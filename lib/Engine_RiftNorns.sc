// Engine_RiftNorns: FASCIA voices for norns. Every note is its own voice (as RIFT's PERC-style
// tracks and live.render_notes): a synth that frees itself when it rests. Past the voice
// limit the oldest is faded out over one block (the DN2's steal). With one voice the engine
// is monophonic: a new note steals the sounding voice in place.
//
// The UGen is looked up when the engine starts (\RiftFascia.asClass), never named in the
// code, so a missing install cannot break the class library for other scripts.
Engine_RiftNorns : CroneEngine {
	var <ugen, group, lfoBus, lfo, voices, ctl, seed = 0, maxVoices = 6, amp = 0.5, pan = 0;

	*new { arg context, doneCallback; ^super.new(context, doneCallback) }

	alloc {
		var names;
		ugen = \RiftFascia.asClass;
		voices = List.new;
		group = Group.new(context.xg);
		lfoBus = Bus.control(context.server, 1);
		if(ugen.isNil) {
			"RIFT-NORNS: the RiftFascia UGen is not installed. Run the rift-norns script once and restart norns (SYSTEM > RESTART).".error;
		} {
			names = ugen.names;
			ctl = Dictionary.new;
			names.do { |n, i| ctl[n] = ugen.defaults[i] };
			SynthDef(\fascia, { |out = 0, t_trig = 1, note = 60, vel = 1, seed = 1, kill = 0,
				amp = 0.5, pan = 0, trem = 0, lfo = 0|
				var c = names.collect { |n, i| NamedControl.kr(n, ugen.defaults[i]) };
				var sig = ugen.ar(t_trig, note, vel, seed, kill, c);
				FreeSelfWhenDone.kr(sig);
				// the DN2's own LFO on AMP, as the audition imitates it: 1 - depth (0.5 + 0.5 sin)
				sig = sig * (1 - (trem * (0.5 + (0.5 * In.kr(lfo)))));
				Out.ar(out, Pan2.ar(sig * amp, pan));
			}).add;
			// one free-running tremolo LFO for every voice (a track LFO, not a per-note one)
			SynthDef(\fasciaLfo, { |out = 0, rate = 0|
				Out.kr(out, SinOsc.kr(rate));
			}).add;
			context.server.sync;
			lfo = Synth.new(\fasciaLfo, [\out, lfoBus, \rate, 0], context.xg, \addToHead);
		};

		this.addCommand("note_on", "ff", { |msg| this.noteOn(msg[1], msg[2]) });
		this.addCommand("param", "si", { |msg| this.setParam(msg[1].asString.toLower.asSymbol, msg[2]) });
		this.addCommand("voices", "i", { |msg| maxVoices = msg[1].clip(1, 32); this.trim });
		this.addCommand("amp", "f", { |msg| amp = msg[1]; group.set(\amp, amp) });
		this.addCommand("pan", "f", { |msg| pan = msg[1]; group.set(\pan, pan) });
		this.addCommand("trem_rate", "f", { |msg| lfo !? { lfo.set(\rate, msg[1]) } });
		this.addCommand("trem_depth", "f", { |msg| ctl !? { ctl[\trem] = msg[1] }; group.set(\trem, msg[1]) });
		this.addCommand("panic", "", { |msg| voices.copy.do { |v| this.release(v) } });
	}

	setParam { |name, value|
		ctl.isNil.if { ^this };
		ctl[name] = value;
		group.set(name, value);
	}

	noteOn { |note, vel|
		var args, syn;
		ugen.isNil.if { ^this };
		seed = (seed % 65535) + 1;
		if(maxVoices == 1 and: { voices.size > 0 }) {
			// mono: steal in place (the voice fades one block, then strikes)
			syn = voices.last;
			syn.set(\note, note, \vel, vel, \t_trig, 1);
			^this
		};
		args = [\out, context.out_b, \t_trig, 1, \note, note, \vel, vel, \seed, seed,
			\amp, amp, \pan, pan, \lfo, lfoBus.index, \trem, ctl[\trem] ? 0];
		ctl.keysValuesDo { |k, v| if(k != \trem) { args = args ++ [k, v] } };
		syn = Synth.new(\fascia, args, group, \addToTail);
		voices.add(syn);
		syn.onFree { voices.remove(syn) };
		this.trim;
	}

	// fade out the oldest voices past the limit
	trim {
		while { voices.size > maxVoices } {
			this.release(voices.first);
		};
	}

	release { |syn|
		voices.remove(syn);
		syn.set(\kill, 1);
	}

	free {
		voices.copy.do { |v| v.free };
		lfo !? { lfo.free };
		group.free;
		lfoBus.free;
	}
}
