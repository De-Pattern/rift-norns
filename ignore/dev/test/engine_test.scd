// live test of Engine_RiftNorns against a stub CroneEngine (scsynth on JACK's dummy driver):
// every machine plays, notes off, the voice limit steals, mono steals in place, switching
// machines fades the old voices, everything frees itself.
(
s = Server.default;
s.options.sampleRate = 48000; s.options.blockSize = 128;
s.waitForBoot {
	var ctx = (server: s, xg: Group.new(s), out_b: Bus.audio(s, 2)), e, buf, nv;
	buf = Buffer.alloc(s, 48000 * 12, 2);
	s.sync;
	{ RecordBuf.ar(In.ar(ctx.out_b, 2), buf, loop: 0, doneAction: 2) }.play(ctx.xg, addAction: \addAfter);
	e = Engine_RiftNorns.new(ctx, { "engine up".postln });
	2.wait;
	nv = { e.instVarAt(\voices).size };
	e.cmd("amp", 0.7);
	[\fascia, \organ, \silt, \ligature, \resonator].do { |m|
		e.cmd("machine", m.asString);
		e.cmd("voices", 3);
		e.cmd("note_on", 60, 0.9); 0.2.wait;
		e.cmd("note_on", 64, 0.7); 0.2.wait;
		e.cmd("note_on", 67, 1.0); 0.2.wait;
		e.cmd("note_on", 71, 0.8); 0.05.wait;   // a fourth: steals the oldest
		("% voices after steal: %".format(m, nv.value)).postln;
		e.cmd("note_off", 64); e.cmd("note_off", 67); e.cmd("note_off", 71);
		e.cmd("param", (TUNE: "TUNE", fascia: "TUNE")[\fascia] ? "TUNE", 70);
		0.6.wait;
		("% voices 0.6 s after note-offs: %".format(m, nv.value)).postln;
	};
	e.cmd("machine", "organ"); e.cmd("voices", 1);
	e.cmd("note_on", 48, 1.0); 0.3.wait; e.cmd("note_on", 55, 1.0); 0.05.wait;
	("organ mono voices: " ++ nv.value).postln;
	e.cmd("note_off", 55); 0.6.wait;
	("organ after its note-off (release 0.1 s): " ++ nv.value).postln;
	e.cmd("machine", "fascia"); e.cmd("voices", 6);
	3.wait;
	("voices at end: " ++ nv.value).postln;
	s.queryAllNodes;
	buf.write("/tmp/engine_test.wav", "WAV", "float");
	s.sync; 1.wait;
	e.free; 1.wait; 0.exit;
};
)
