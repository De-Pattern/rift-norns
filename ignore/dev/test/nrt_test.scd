// NRT check: the real UGen in scsynth against the offline renderer.
// sclang test/nrt_test.scd <spec file> <out.raw>   (spec: same P/S/E lines as render.cpp)
(
var args = thisProcess.argv;
var specPath = args[0], outPath = args[1];
var lines = File.readAllString(specPath).split($\n).reject(_.isEmpty);
var ctl, secs = 1, evs = List.new, score, def, wav = outPath ++ ".wav";
lines.do { |l|
	var w = l.split($ );
	switch(w[0],
		"P", { ctl = w[1..].collect(_.asInteger) },
		"S", { secs = w[1].asFloat },
		"E", { evs.add(w[1..].collect(_.asFloat)) }
	);
};
def = SynthDef(\fasciaTest, { |note = 60, vel = 1, seed = 1|
	var c = RiftFascia.names.collect { |n, i| NamedControl.kr(n, ctl[i]) };
	var sig = RiftFascia.ar(1, note, vel, seed, 0, c);
	FreeSelfWhenDone.kr(sig);
	Out.ar(0, sig);
});
score = Score.new;
score.add([0.0, ['/d_recv', def.asBytes]]);
evs.do { |e, k|
	score.add([e[0], Synth.basicNew(\fasciaTest, Server.default, 1000 + k).newMsg(1, [\note, e[1], \vel, e[2], \seed, k + 1], \addToTail)]);
};
score.add([secs, ['/c_set', 0, 0]]);
score.recordNRT(nil, wav, nil, 48000, "WAV", "float",
	ServerOptions.new.numOutputBusChannels_(1).sampleRate_(48000).blockSize_(64).verbosity_(-1),
	duration: secs, action: { 0.exit });
)
