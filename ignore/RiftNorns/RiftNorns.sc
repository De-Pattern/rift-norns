// FASCIA — RIFT's physical/spectral machine, one voice a UGen.
// RiftFascia.ar(trig, note, vel, seed, kill, *controls): controls are the 32 engine controls in
// RIFT's page order (RiftFascia.names), 0..127.
RiftFascia : UGen {
	*names {
		// lower case: a SynthDef's control names must not read as class names
		^#[\typa, \typb, \hard, \dust, \tzfm, \bend, \mix, \dec,
			\boda, \bodb, \tune, \geom, \matl, \pos, \coup, \damp,
			\morph, \form, \shift, \ring, \side, \fold, \lpg, \char,
			\atk, \clik, \ctyp, \xmod, \fdbk, \buzz, \watr, \vary]
	}
	// MACHINE SEL's start (RIFT v0.4: the demo's vibraphone)
	*defaults {
		^#[0, 1, 42, 20, 0, 10, 0, 20, 49, 96, 64, 64, 96, 28, 70, 34,
			0, 0, 64, 0, 40, 0, 20, 0, 0, 0, 0, 0, 0, 0, 0, 40]
	}
	*ar { arg trig = 1, note = 60, vel = 1, seed = 1, kill = 0 ... controls;
		var c = controls.flat;
		if(c.size != 32) {
			c = this.defaults.collect { |d, i| c[i] ? d };
		};
		^this.multiNewList(['audio', trig, note, vel, seed, kill] ++ c)
	}
}
