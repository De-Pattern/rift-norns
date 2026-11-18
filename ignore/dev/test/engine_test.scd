// live test of Engine_RiftNorns against a stub CroneEngine (scsynth on JACK's dummy driver)
(
s = Server.default;
s.options.sampleRate = 48000; s.options.blockSize = 128;
s.waitForBoot {
	var ctx = (server: s, xg: Group.new(s), out_b: Bus.audio(s, 2)), e, buf, rec;
	buf = Buffer.alloc(s, 48000 * 6, 2);
	s.sync;
	rec = { RecordBuf.ar(In.ar(ctx.out_b, 2), buf, loop: 0, doneAction: 2) }.play(ctx.xg, addAction: \addAfter);
	e = Engine_RiftNorns.new(ctx, { "engine up".postln });
	2.wait;
	("ugen class: " ++ e.ugen).postln;
	e.cmd("amp", 0.7);
	e.cmd("voices", 4);
	e.cmd("note_on", 60, 0.9);
	0.3.wait; e.cmd("note_on", 64, 0.7);
	0.3.wait; e.cmd("param", "BODA", 64); e.cmd("note_on", 67, 1.0);
	0.3.wait; e.cmd("note_on", 72, 0.5);
	0.3.wait; e.cmd("note_on", 76, 0.8);  // fifth note: steals the oldest (limit 4)
	0.1.wait; ("voices after steal: " ++ e.instVarAt(\voices).size).postln;
	e.cmd("trem_depth", 0.35); e.cmd("trem_rate", 5.5);
	e.cmd("voices", 1);
	0.3.wait; e.cmd("note_on", 48, 1.0);
	0.2.wait; e.cmd("note_on", 55, 1.0);  // mono steal in place
	0.1.wait; ("voices mono: " ++ e.instVarAt(\voices).size).postln;
	3.wait;
	s.queryAllNodes;
	buf.write("/tmp/engine_test.wav", "WAV", "float");
	s.sync;
	1.wait;
	("voices at end: " ++ e.instVarAt(\voices).size).postln;
	e.free;
	1.wait;
	0.exit;
};
)
