// NRT: each machine's UGen in scsynth, for a spec file of "machine seed note vel offblock secs k0 k1 ..."
// lines; writes OUT_k.wav per line.  sclang nrt_machines.scd <spec> <outprefix>
(
var args = thisProcess.argv, lines = File.readAllString(args[0]).split($\n).reject(_.isEmpty);
var prefix = args[1], cls = (fascia: \RiftFascia, organ: \RiftOrgan, silt: \RiftSilt,
	ligature: \RiftLigature, resonator: \RiftResonator);
var go = { |i|
	var w = lines[i].split($ ), m = w[0].asSymbol, seed = w[1].asInteger, note = w[2].asFloat,
	vel = w[3].asFloat, offb = w[4].asInteger, secs = w[5].asFloat, k = w[6..].collect(_.asInteger);
	var u = cls[m].asClass, score = Score.new, def;
	def = SynthDef(\t, { |gate = 1|
		var c = u.names.collect { |n, j| NamedControl.kr(n, k[j]) };
		Out.ar(0, u.ar(1, note, vel, seed, 0, gate, 0.1, 120, c));
	});
	score.add([0.0, ['/d_recv', def.asBytes]]);
	score.add([0.0, Synth.basicNew(\t, Server.default, 1000).newMsg(1, [], \addToTail)]);
	if(offb > 0) { score.add([offb * 32 / 48000, ['/n_set', 1000, \gate, 0]]) };
	score.add([secs, ['/c_set', 0, 0]]);
	score.recordNRT(nil, prefix ++ "_" ++ i ++ ".wav", nil, 48000, "WAV", "float",
		ServerOptions.new.numOutputBusChannels_(1).sampleRate_(48000).blockSize_(64).verbosity_(-1),
		duration: secs, action: { if(i + 1 < lines.size) { go.(i + 1) } { 0.exit } });
};
go.(0);
)
