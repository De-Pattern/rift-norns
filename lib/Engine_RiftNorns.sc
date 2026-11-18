// Engine_RiftNorns: RIFT's machines on norns, one machine at a time (its MACHINE SEL).
// Every note is its own voice (as RIFT's tracks and live.py's render_notes): a synth that frees
// itself when it rests. Past the voice limit the oldest is faded out over one block (the DN2's
// steal). With one voice the engine is monophonic: a new note steals the sounding voice in place.
// A note-off closes the gate of that note's voices: SILT and LIGATURE release on their own,
// ORGAN fades over RELEASE (the DN2's amp envelope), FASCIA and RESONATOR ring on.
//
// The UGens are looked up when the engine starts (asClass), never named in the code, so a missing
// install cannot break the class library for other scripts.
Engine_RiftNorns : CroneEngine {
	classvar <machines;
	var ugens, ctl, group, lfoBus, lfo, voices, cur, seed = 0, maxVoices = 6, amp = 0.5, pan = 0,
	trem = 0, rel = 0.5, bpm = 120, ready = false;

	*initClass {
		machines = [\fascia, \RiftFascia, \organ, \RiftOrgan, \silt, \RiftSilt,
			\ligature, \RiftLigature, \resonator, \RiftResonator];
	}

	*new { arg context, doneCallback; ^super.new(context, doneCallback) }

	alloc {
		voices = List.new;
		ugens = Dictionary.new;
		ctl = Dictionary.new;
		group = Group.new(context.xg);
		lfoBus = Bus.control(context.server, 1);
		machines.pairsDo { |id, cls|
			var u = cls.asClass;
			if(u.notNil) {
				ugens[id] = u;
				ctl[id] = Dictionary.new;
				u.names.do { |n, i| ctl[id][n] = u.defaults[i] };
				SynthDef(("rift_" ++ id).asSymbol, { |out = 0, t_trig = 1, note = 60, vel = 1, seed = 1,
					kill = 0, gate = 1, rel = 0.1, bpm = 120, amp = 0.5, pan = 0, trem = 0, lfo = 0|
					var c = u.names.collect { |n, i| NamedControl.kr(n, u.defaults[i]) };
					var sig = u.ar(t_trig, note, vel, seed, kill, gate, rel, bpm, c);
					FreeSelfWhenDone.kr(sig);
					// the DN2's own LFO on AMP, as FASCIA's audition imitates it: 1 - depth (0.5 + 0.5 sin)
					sig = sig * (1 - (trem * (0.5 + (0.5 * In.kr(lfo)))));
					Out.ar(out, Pan2.ar(sig * amp, pan));
				}).add;
			};
		};
		if(ugens.isEmpty) {
			"RIFT-NORNS: its UGens are not installed. Run the rift-norns script once and restart norns (SYSTEM > RESTART).".error;
		} {
			// one free-running tremolo LFO for every voice (a track LFO, not a per-note one)
			SynthDef(\rift_lfo, { |out = 0, rate = 5.5| Out.kr(out, SinOsc.kr(rate)) }).add;
			context.server.sync;
			lfo = Synth.new(\rift_lfo, [\out, lfoBus], context.xg, \addToHead);
			cur = \fascia;
			ready = true;
		};

		this.addCommand("machine", "s", { |msg| this.setMachine(msg[1].asSymbol) });
		this.addCommand("note_on", "ff", { |msg| this.noteOn(msg[1], msg[2]) });
		this.addCommand("note_off", "f", { |msg| this.noteOff(msg[1]) });
		this.addCommand("param", "si", { |msg| this.setParam(("rn" ++ msg[1].asString.toLower).asSymbol, msg[2]) });
		this.addCommand("voices", "i", { |msg| maxVoices = msg[1].clip(1, 32); this.trim });
		this.addCommand("amp", "f", { |msg| amp = msg[1]; group.set(\amp, amp) });
		this.addCommand("pan", "f", { |msg| pan = msg[1]; group.set(\pan, pan) });
		this.addCommand("release", "f", { |msg| rel = msg[1]; group.set(\rel, rel) });
		this.addCommand("bpm", "f", { |msg| bpm = msg[1] });
		this.addCommand("trem_rate", "f", { |msg| lfo !? { lfo.set(\rate, msg[1]) } });
		this.addCommand("trem_depth", "f", { |msg| trem = msg[1]; group.set(\trem, trem) });
		this.addCommand("panic", "", { |msg| voices.copy.do { |v| this.release(v) } });
	}

	setMachine { |id|
		if(ugens.isNil or: { ugens[id].isNil }) { ^this };
		if(id != cur) {
			voices.copy.do { |v| this.release(v) };
			cur = id;
		};
	}

	setParam { |name, value|
		var d;
		if(ready.not) { ^this };
		d = ctl[cur];
		if(d.notNil and: { d.includesKey(name) }) {
			d[name] = value;
			group.set(name, value);
		};
	}

	noteOn { |note, vel|
		var args, syn, last;
		if(ready.not) { ^this };
		seed = (seed % 65535) + 1;
		if(maxVoices == 1 and: { voices.size > 0 }) {
			// mono: steal in place (the voice fades one block, then strikes)
			last = voices.last;
			last[1] = note;
			last[0].set(\note, note, \vel, vel, \gate, 1, \t_trig, 1);
			^this
		};
		args = [\out, context.out_b, \t_trig, 1, \note, note, \vel, vel, \seed, seed, \gate, 1,
			\rel, rel, \bpm, bpm, \amp, amp, \pan, pan, \lfo, lfoBus.index, \trem, trem];
		ctl[cur].keysValuesDo { |k, v| args = args ++ [k, v] };
		syn = Synth.new(("rift_" ++ cur).asSymbol, args, group, \addToTail);
		syn = [syn, note];
		voices.add(syn);
		syn[0].onFree { voices.remove(syn) };
		this.trim;
	}

	noteOff { |note|
		voices.do { |v| if(v[1] == note) { v[0].set(\gate, 0) } };
	}

	// fade out the oldest voices past the limit
	trim {
		while { voices.size > maxVoices } { this.release(voices.first) };
	}

	release { |v|
		voices.remove(v);
		v[0].set(\kill, 1);
	}

	free {
		voices.copy.do { |v| v[0].free };
		lfo !? { lfo.free };
		group.free;
		lfoBus.free;
	}
}
