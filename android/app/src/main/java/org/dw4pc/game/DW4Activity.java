package org.dw4pc.game;

import android.content.ComponentCallbacks2;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.res.Configuration;
import android.hardware.display.DisplayManager;
import android.hardware.input.InputManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/**
 * The app's activity: SDL's SDLActivity (its Java sources are copied from the
 * SDL source tree at build time, android/app/build.gradle) with what DW4_PC
 * adds on Android. Phase A (docs/ANDROID.md): the display report the native
 * probe logs, display events in logcat, and arguments from an intent extra.
 * Phase C: the window full screen without the status and navigation bars
 * (immersive, kept after the menu, HOME and back and a controller coming
 * again), the game's frame rate asked of the display, and memory warnings
 * passed on only when memory is short.
 */
public class DW4Activity extends SDLActivity {
    private static final String TAG = "DW4probe";
    private static final String GAME_TAG = "DW4";

    private DisplayManager.DisplayListener mDisplayListener;
    private InputManager.InputDeviceListener mInputDeviceListener;
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());
    private final Runnable mImmersive = this::applyImmersive;
    private final Runnable mReportWindow = this::reportWindow;

    /** The frame rate the game asks of the display (setFrameRate). */
    private static volatile float sFrameRate = 60.0f;

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new GameSurface(context);
    }

    /**
     * SDL's surface; every new or changed surface gets the game's frame rate
     * (the surface made again after HOME has none of its own).
     */
    static class GameSurface extends SDLSurface {
        GameSurface(Context context) {
            super(context);
        }

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            super.surfaceChanged(holder, format, width, height);
            applyFrameRate(holder.getSurface(), "surface " + width + "x" + height);
        }
    }

    /**
     * The game's 60 updates a second on a panel that runs 60 or 120 Hz (the
     * Thor's top screen): FIXED_SOURCE asks for a display rate the game's
     * rate divides evenly, so no picture stays longer than the next one.
     * ALWAYS lets the display switch even when the switch is not seamless (a
     * game keeps its rate for a long time).
     */
    static void applyFrameRate(Surface surface, String why) {
        if (surface == null || !surface.isValid()) {
            return;
        }
        final float rate = sFrameRate;
        try {
            surface.setFrameRate(rate, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE,
                    Surface.CHANGE_FRAME_RATE_ALWAYS);
            Log.i(GAME_TAG, "display: frame rate " + rate + " Hz asked (" + why + ")");
        } catch (RuntimeException e) {
            Log.w(GAME_TAG, "display: frame rate " + rate + " Hz refused: " + e);
        }
    }

    /**
     * Called by the game (JNI, platform_android.cpp) with the rate it
     * presents at: 60, or a frame limit above 60 the player chose.
     */
    public static void setFrameRate(float rate) {
        if (rate <= 0.0f) {
            return;
        }
        sFrameRate = rate;
        final SDLActivity activity = mSingleton;
        if (activity instanceof DW4Activity) {
            ((DW4Activity) activity).mMainHandler.post(() -> {
                if (mSurface != null) {
                    applyFrameRate(mSurface.getHolder().getSurface(), "the game");
                }
            });
        }
    }

    /**
     * Full screen on the game's display: no status bar, no navigation bar (a
     * swipe from the edge shows them for a moment; they go again on their
     * own), the picture also under a display cutout. Applied when the window
     * is made and again whenever it could have been undone: focus coming
     * back (the keyboard of a text field, HOME and back, the app switcher), a
     * controller coming or going, a configuration change.
     */
    private void applyImmersive() {
        Window window = getWindow();
        if (window == null) {
            return;
        }
        WindowManager.LayoutParams attributes = window.getAttributes();
        if (attributes.layoutInDisplayCutoutMode != WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS) {
            attributes.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
            window.setAttributes(attributes);
        }
        window.setDecorFitsSystemWindows(false);
        WindowInsetsController controller = window.getInsetsController();
        if (controller != null) {
            controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            controller.hide(WindowInsets.Type.systemBars());
        }
        mMainHandler.removeCallbacks(mReportWindow);
        mMainHandler.postDelayed(mReportWindow, 600);
    }

    private void scheduleImmersive(long delayMs) {
        mMainHandler.removeCallbacks(mImmersive);
        mMainHandler.postDelayed(mImmersive, delayMs);
    }

    /** The window after applyImmersive, in logcat (tag DW4). */
    private void reportWindow() {
        Window window = getWindow();
        View decor = window != null ? window.getDecorView() : null;
        if (decor == null) {
            return;
        }
        WindowInsets insets = decor.getRootWindowInsets();
        String bars = insets == null ? "bars unknown"
                : "status bar " + (insets.isVisible(WindowInsets.Type.statusBars()) ? "shown" : "hidden")
                  + ", navigation bar " + (insets.isVisible(WindowInsets.Type.navigationBars()) ? "shown" : "hidden");
        Display display = getDisplay();
        String rate = display != null ? String.format(java.util.Locale.ROOT, ", display %d at %.2f Hz",
                display.getDisplayId(), display.getRefreshRate()) : "";
        Log.i(GAME_TAG, "window: " + decor.getWidth() + "x" + decor.getHeight() + ", " + bars + rate);
    }

    @Override
    protected void onResume() {
        super.onResume();
        scheduleImmersive(0);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            scheduleImmersive(0);
        }
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        scheduleImmersive(100);
    }

    /**
     * SDL passes every onTrimMemory on as SDL_EVENT_LOW_MEMORY, also
     * TRIM_MEMORY_UI_HIDDEN, which only says the app's window went out of
     * sight (every HOME): the game would let its texture cache go each time.
     * Only the levels that mean memory is short reach the game.
     */
    @Override
    public void onTrimMemory(int level) {
        if (level == ComponentCallbacks2.TRIM_MEMORY_UI_HIDDEN) {
            Log.i(GAME_TAG, "lifecycle: trim memory level " + level + " (the window is hidden; not passed on)");
            return;
        }
        Log.i(GAME_TAG, "lifecycle: trim memory level " + level + " (passed on as low memory)");
        super.onTrimMemory(level);
    }

    @Override
    protected String[] getLibraries() {
        // SDLActivity loads these in order and calls SDL_main in the last one.
        return new String[] {"SDL3", "main"};
    }

    /**
     * Command line arguments: adb shell am start ... --es dw4args "--seconds 10".
     * Debuggable builds only: the launcher activity is exported, so in a
     * release build any app could start it with arguments; there the extra is
     * ignored.
     */
    @Override
    protected String[] getArguments() {
        Intent intent = getIntent();
        String args = intent != null ? intent.getStringExtra("dw4args") : null;
        if (args == null || args.trim().isEmpty()) {
            return new String[0];
        }
        if ((getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0) {
            Log.w(TAG, "dw4args ignored: not a debuggable build");
            return new String[0];
        }
        return args.trim().split("\\s+");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        scheduleImmersive(0);
        InputManager im = (InputManager) getSystemService(Context.INPUT_SERVICE);
        if (im != null) {
            // A controller coming again (a USB reconnect, the Thor's
            // controller style switched) can bring the bars back.
            mInputDeviceListener = new InputManager.InputDeviceListener() {
                @Override
                public void onInputDeviceAdded(int deviceId) {
                    scheduleImmersive(300);
                }

                @Override
                public void onInputDeviceRemoved(int deviceId) {
                    scheduleImmersive(300);
                }

                @Override
                public void onInputDeviceChanged(int deviceId) {
                    scheduleImmersive(300);
                }
            };
            im.registerInputDeviceListener(mInputDeviceListener, mMainHandler);
        }
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
        mMainHandler.removeCallbacks(mImmersive);
        mMainHandler.removeCallbacks(mReportWindow);
        InputManager im = (InputManager) getSystemService(Context.INPUT_SERVICE);
        if (im != null && mInputDeviceListener != null) {
            im.unregisterInputDeviceListener(mInputDeviceListener);
        }
        mInputDeviceListener = null;
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
