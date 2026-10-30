// SPDX-License-Identifier: MPL-2.0
// Records an audio input as the macOS driver hands it over (CoreAudio's HAL unit, no resampling), every channel, to
// a 16-bit WAV: for tools/uac_check.py (ffmpeg's AVFoundation input reorders and drops its buffers under load).
//   swiftc -O tools/uac_record.swift -o uac_record && ./uac_record "Terykons FM-1" 30 rec.wav
// Prints the gaps the HAL itself reports (a callback's sample time not following the last one's).
import AudioToolbox
import CoreAudio
import Foundation

func fail(_ s: String) -> Never { FileHandle.standardError.write((s + "\n").data(using: .utf8)!); exit(1) }

func prop<T>(_ id: AudioObjectID, _ sel: AudioObjectPropertySelector, _ scope: AudioObjectPropertyScope, _ v: inout T) -> OSStatus {
    var a = AudioObjectPropertyAddress(mSelector: sel, mScope: scope, mElement: kAudioObjectPropertyElementMain)
    var n = UInt32(MemoryLayout<T>.size)
    return withUnsafeMutablePointer(to: &v) { AudioObjectGetPropertyData(id, &a, 0, nil, &n, $0) }
}

func inputChannels(_ id: AudioObjectID) -> Int {
    var a = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyStreamConfiguration,
                                       mScope: kAudioObjectPropertyScopeInput, mElement: kAudioObjectPropertyElementMain)
    var n: UInt32 = 0
    guard AudioObjectGetPropertyDataSize(id, &a, 0, nil, &n) == noErr, n > 0 else { return 0 }
    let raw = UnsafeMutableRawPointer.allocate(byteCount: Int(n), alignment: 16)
    defer { raw.deallocate() }
    guard AudioObjectGetPropertyData(id, &a, 0, nil, &n, raw) == noErr else { return 0 }
    let list = UnsafeMutableAudioBufferListPointer(raw.assumingMemoryBound(to: AudioBufferList.self))
    return list.reduce(0) { $0 + Int($1.mNumberChannels) }
}

func findDevice(_ want: String) -> (AudioObjectID, String) {
    var a = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDevices, mScope: kAudioObjectPropertyScopeGlobal,
                                       mElement: kAudioObjectPropertyElementMain)
    var n: UInt32 = 0
    AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &a, 0, nil, &n)
    var ids = [AudioObjectID](repeating: 0, count: Int(n) / MemoryLayout<AudioObjectID>.size)
    AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &a, 0, nil, &n, &ids)
    for id in ids {
        var name: Unmanaged<CFString>? = nil
        guard prop(id, kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, &name) == noErr,
              let s = name?.takeRetainedValue() as String? else { continue }
        if s.lowercased().contains(want.lowercased()) && inputChannels(id) > 0 { return (id, s) }
    }
    fail("no audio input named like \"\(want)\"")
}

final class Rec {
    var unit: AudioUnit? = nil
    var ch = 0
    var buf: UnsafeMutablePointer<Float32>
    var cap = 0, frames = 0
    var scratch: UnsafeMutablePointer<Float32>
    var lastTime = -1.0, gaps = 0, gapFrames = 0.0, callbacks = 0, renderErr: OSStatus = 0
    init(ch: Int, cap: Int) {
        self.ch = ch; self.cap = cap
        buf = .allocate(capacity: cap * ch)
        scratch = .allocate(capacity: 8192 * ch)
    }
}

let args = CommandLine.arguments
if args.count < 4 { fail("usage: uac_record DEVICE SECONDS OUT.wav") }
let (dev, devName) = findDevice(args[1])
let seconds = Double(args[2]) ?? 10
var rate: Float64 = 0
_ = prop(dev, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, &rate)
let ch = inputChannels(dev)
print("device   \(devName): \(ch) input channels, \(Int(rate)) Hz")

var desc = AudioComponentDescription(componentType: kAudioUnitType_Output, componentSubType: kAudioUnitSubType_HALOutput,
                                     componentManufacturer: kAudioUnitManufacturer_Apple, componentFlags: 0, componentFlagsMask: 0)
guard let comp = AudioComponentFindNext(nil, &desc) else { fail("no HAL unit") }
let rec = Rec(ch: ch, cap: Int(seconds * rate) + 8192)
var st = AudioComponentInstanceNew(comp, &rec.unit)
guard st == noErr, let unit = rec.unit else { fail("unit: \(st)") }
var one: UInt32 = 1, zero: UInt32 = 0, devID = dev
AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &one, 4)
AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &zero, 4)
st = AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &devID,
                          UInt32(MemoryLayout<AudioObjectID>.size))
if st != noErr { fail("device: \(st)") }
var fmt = AudioStreamBasicDescription(mSampleRate: rate, mFormatID: kAudioFormatLinearPCM,
                                      mFormatFlags: kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
                                      mBytesPerPacket: UInt32(4 * ch), mFramesPerPacket: 1, mBytesPerFrame: UInt32(4 * ch),
                                      mChannelsPerFrame: UInt32(ch), mBitsPerChannel: 32, mReserved: 0)
st = AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &fmt,
                          UInt32(MemoryLayout<AudioStreamBasicDescription>.size))
if st != noErr { fail("format: \(st)") }

let callback: AURenderCallback = { refCon, flags, ts, bus, n, _ in
    let r = Unmanaged<Rec>.fromOpaque(refCon).takeUnretainedValue()
    var list = AudioBufferList(mNumberBuffers: 1, mBuffers: AudioBuffer(mNumberChannels: UInt32(r.ch),
                               mDataByteSize: n * UInt32(4 * r.ch), mData: UnsafeMutableRawPointer(r.scratch)))
    let e = AudioUnitRender(r.unit!, flags, ts, bus, n, &list)
    if e != noErr { r.renderErr = e; return e }
    r.callbacks += 1
    let t = ts.pointee.mSampleTime
    if r.lastTime >= 0 && t != r.lastTime { r.gaps += 1; r.gapFrames += t - r.lastTime }
    r.lastTime = t + Double(n)
    let k = min(Int(n), r.cap - r.frames)
    if k > 0 { (r.buf + r.frames * r.ch).update(from: r.scratch, count: k * r.ch); r.frames += k }
    return noErr
}
var cb = AURenderCallbackStruct(inputProc: callback, inputProcRefCon: Unmanaged.passUnretained(rec).toOpaque())
AudioUnitSetProperty(unit, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &cb,
                     UInt32(MemoryLayout<AURenderCallbackStruct>.size))
st = AudioUnitInitialize(unit)
if st != noErr { fail("initialize: \(st)") }
st = AudioOutputUnitStart(unit)
if st != noErr { fail("start: \(st)") }
Thread.sleep(forTimeInterval: seconds)
AudioOutputUnitStop(unit)
AudioUnitUninitialize(unit)
print("recorded \(rec.frames) frames in \(rec.callbacks) callbacks; HAL gaps \(rec.gaps) (\(Int(rec.gapFrames)) frames)" +
      (rec.renderErr != noErr ? "; render error \(rec.renderErr)" : ""))

// 16-bit WAV: each float back to the 16-bit sample it was (x 32768, rounded)
var d = Data()
func le<T: FixedWidthInteger>(_ v: T) { withUnsafeBytes(of: v.littleEndian) { d.append(contentsOf: $0) } }
let bytes = rec.frames * ch * 2
d.append("RIFF".data(using: .ascii)!); le(UInt32(36 + bytes)); d.append("WAVEfmt ".data(using: .ascii)!)
le(UInt32(16)); le(UInt16(1)); le(UInt16(ch)); le(UInt32(rate)); le(UInt32(Int(rate) * ch * 2)); le(UInt16(ch * 2)); le(UInt16(16))
d.append("data".data(using: .ascii)!); le(UInt32(bytes))
var pcm = [Int16](repeating: 0, count: rec.frames * ch)
for i in 0..<pcm.count { pcm[i] = Int16(max(-32768, min(32767, (rec.buf[i] * 32768).rounded()))) }
pcm.withUnsafeBytes { d.append(contentsOf: $0) }
do { try d.write(to: URL(fileURLWithPath: args[3])) } catch { fail("write: \(error)") }
print("wrote    \(args[3])")
