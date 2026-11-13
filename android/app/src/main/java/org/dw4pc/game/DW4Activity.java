package org.dw4pc.game;

import android.content.Context;
import android.content.Intent;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;

import org.libsdl.app.SDLActivity;

/**
 * The app's activity: SDL's SDLActivity (its Java sources are copied from the
 * SDL source tree at build time, android/app/build.gradle) with what DW4_PC
 * adds on Android. Phase A (docs/ANDROID.md): the display report the native
 * probe logs, display events in logcat, and arguments from an intent extra.
 */
public class DW4Activity extends SDLActivity {
    private static final String TAG = "DW4probe";

    private DisplayManager.DisplayListener mDisplayListener;

    @Override
    protected String[] getLibraries() {
        // SDLActivity loads these in order and calls SDL_main in the last one.
        return new String[] {"SDL3", "main"};
    }

    /** Command line arguments: adb shell am start ... --es dw4args "--seconds 10". */
    @Override
    protected String[] getArguments() {
        Intent intent = getIntent();
        String args = intent != null ? intent.getStringExtra("dw4args") : null;
        if (args == null || args.trim().isEmpty()) {
            return new String[0];
        }
        return args.trim().split("\\s+");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        DisplayManager dm = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
        if (dm != null) {
            mDisplayListener = new DisplayManager.DisplayListener() {
                @Override
                public void onDisplayAdded(int displayId) {
                    Log.i(TAG, "display event: added " + displayId);
                }

                @Override
                public void onDisplayRemoved(int displayId) {
                    Log.i(TAG, "display event: removed " + displayId);
                }

                @Override
                public void onDisplayChanged(int displayId) {
                    Display d = dm.getDisplay(displayId);
                    Log.i(TAG, "display event: changed " + displayId
                            + (d != null ? " state " + stateName(d.getState()) : ""));
                }
            };
            dm.registerDisplayListener(mDisplayListener, new Handler(Looper.getMainLooper()));
        }
    }

    @Override
    protected void onDestroy() {
        DisplayManager dm = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
        if (dm != null && mDisplayListener != null) {
            dm.unregisterDisplayListener(mDisplayListener);
        }
        mDisplayListener = null;
        super.onDestroy();
    }

    private static String stateName(int state) {
        switch (state) {
            case Display.STATE_OFF: return "OFF";
            case Display.STATE_ON: return "ON";
            case Display.STATE_DOZE: return "DOZE";
            case Display.STATE_DOZE_SUSPEND: return "DOZE_SUSPEND";
            case Display.STATE_VR: return "VR";
            case Display.STATE_ON_SUSPEND: return "ON_SUSPEND";
            default: return "UNKNOWN(" + state + ")";
        }
    }

    /**
     * The public Display.FLAG_* names; other bits are AOSP-internal flags and
     * stay numbered (bitN), the raw value is printed beside them.
     */
    private static String flagNames(int flags) {
        StringBuilder sb = new StringBuilder();
        String[] names = {"SUPPORTS_PROTECTED_BUFFERS", "SECURE", "PRIVATE", "PRESENTATION", "ROUND"};
        for (int bit = 0; bit < 32; ++bit) {
            if ((flags & (1 << bit)) == 0) {
                continue;
            }
            if (sb.length() > 0) {
                sb.append('|');
            }
            sb.append(bit < names.length ? names[bit] : "bit" + bit);
        }
        return sb.length() > 0 ? sb.toString() : "none";
    }

    /**
     * Every display Android's DisplayManager reports, one line each, for the
     * native probe's log (src/android/probe_main.cpp). Phase E picks the
     * Thor's lower screen from this: FLAG_PRESENTATION, not PRIVATE, not the
     * activity's own display, state not OFF.
     */
    public static String displayReport() {
        Context context = mSingleton;
        StringBuilder sb = new StringBuilder();
        sb.append("device ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
          .append(" (").append(Build.DEVICE).append(", ").append(Build.PRODUCT).append(") Android ")
          .append(Build.VERSION.RELEASE).append(" API ").append(Build.VERSION.SDK_INT)
          .append(" ABIs ").append(String.join(",", Build.SUPPORTED_ABIS)).append('\n');
        if (context == null) {
            return sb.append("no context").toString();
        }
        DisplayManager dm = (DisplayManager) context.getSystemService(Context.DISPLAY_SERVICE);
        if (dm == null) {
            return sb.append("no DisplayManager").toString();
        }
        Display ownDisplay = mSingleton.getDisplay();
        int own = ownDisplay != null ? ownDisplay.getDisplayId() : -1;
        Display[] displays = dm.getDisplays();
        sb.append("displays ").append(displays.length).append(", the activity's display ").append(own).append('\n');
        for (Display d : displays) {
            Display.Mode mode = d.getMode();
            DisplayMetrics metrics = new DisplayMetrics();
            d.getRealMetrics(metrics);
            sb.append("id ").append(d.getDisplayId())
              .append(" '").append(d.getName()).append('\'')
              .append(" flags 0x").append(Integer.toHexString(d.getFlags()))
              .append(' ').append(flagNames(d.getFlags()))
              .append(" state ").append(stateName(d.getState()))
              .append(" valid ").append(d.isValid())
              .append(" mode ").append(mode.getPhysicalWidth()).append('x').append(mode.getPhysicalHeight())
              .append('@').append(String.format(java.util.Locale.ROOT, "%.2f", mode.getRefreshRate()))
              .append(" real ").append(metrics.widthPixels).append('x').append(metrics.heightPixels)
              .append(" dpi ").append(metrics.densityDpi)
              .append(" rotation ").append(d.getRotation());
            Display.Mode[] modes = d.getSupportedModes();
            if (modes.length > 1) {
                sb.append(" modes");
                for (Display.Mode m : modes) {
                    sb.append(' ').append(m.getPhysicalWidth()).append('x').append(m.getPhysicalHeight())
                      .append('@').append(String.format(java.util.Locale.ROOT, "%.0f", m.getRefreshRate()));
                }
            }
            sb.append('\n');
        }
        Display[] presentation = dm.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
        sb.append("presentation displays:");
        for (Display d : presentation) {
            sb.append(' ').append(d.getDisplayId());
        }
        if (presentation.length == 0) {
            sb.append(" none");
        }
        return sb.toString();
    }
}
