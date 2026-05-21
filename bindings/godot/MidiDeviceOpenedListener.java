package com.yellowlab.rtmidi;

import android.media.midi.MidiDevice;
import android.media.midi.MidiManager;

/**
 * This class must be included in the Android app using rtmidi.
 * Fixed version for SynthCanvas: Added System.loadLibrary to ensure JNI linking.
 * Original version is in RtMidi Library(libs/rtmidi/contrib/java/MidiDeviceOpenedListener.java).
 */
public class MidiDeviceOpenedListener implements MidiManager.OnDeviceOpenedListener {
    static {
        System.loadLibrary("synth_canvas_gdextension");
    }

    private long nativeId;
    private boolean isOutput;

    public MidiDeviceOpenedListener(long id, boolean output) {
        nativeId = id;
        isOutput = output;
    }

    @Override
    public void onDeviceOpened(MidiDevice midiDevice) {
        midiDeviceOpened(midiDevice, nativeId, isOutput);
    }

    private native static void midiDeviceOpened(MidiDevice midiDevice, long id, boolean isOutput);
}
