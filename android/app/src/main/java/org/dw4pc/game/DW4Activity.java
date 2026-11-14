package org.dw4pc.game;

import android.content.ComponentCallbacks2;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.res.Configuration;
import android.graphics.Rect;
import android.hardware.display.DisplayManager;
import android.hardware.input.InputManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

import java.io.File;

/**
 * The app's activity: SDL's SDLActivity (its Java sources are copied from the
 * SDL source tree at build time, android/app/build.gradle) with what DW4_PC
 * adds on Android. Phase A (docs/ANDROID.md): the display report the native
 * probe logs, display events in logcat, and arguments from an intent extra.
 * Phase C: the window full screen without the status and navigation bars
 * (immersive, kept after the menu, HOME and back and a controller coming
 * again), the game's frame rate asked of the display, and memory warnings
 * passed on only when memory is short. Phase E: the second screen's panels
 * on the AYN Thor's lower screen (LowerScreen).
 */
public class DW4Activity extends SDLActivity {
    private static final String TAG = "DW4probe";
    private static final String GAME_TAG = "DW4";

    private DisplayManager.DisplayListener mDisplayListener;
    private LowerScreen mLowerScreen;
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
            final LowerScreen lower = lowerScreen();
            if (lower != null) {
                lower.gameSurfaceCame();
            }
        }

        /**
         * SDL releases the EGL surface after waiting at most 0.5 s for the
         * game's thread to reach its pause (onNativeSurfaceDestroyed); a
         * stage load keeps that thread longer (the debug build: about 1.1 s
         * on the emulator), and the game's next swap met the released
         * surface (EGL_BAD_SURFACE). The pause is asked for first, and the
         * game gets up to 3 s to reach it before SDL goes on.
         */
        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            // (The second screen's surface goes before the game's pause.)
            final LowerScreen lower = lowerScreen();
            if (lower != null) {
                lower.gameSurfaceGone();
            }
            SDLActivity.mNextNativeState = SDLActivity.NativeState.PAUSED;
            SDLActivity.handleNativeState();
            waitForGamePause(3000);
            super.surfaceDestroyed(holder);
        }
    }

    /** The running activity's second screen (null before onCreate and after onDestroy). */
    static LowerScreen lowerScreen() {
        final SDLActivity activity = mSingleton;
        return activity instanceof DW4Activity ? ((DW4Activity) activity).mLowerScreen : null;
    }

    /** DID_ENTER_BACKGROUND reached the game's thread (platform_android.cpp). */
    static native boolean nativeGamePaused();

    static void waitForGamePause(long timeoutMs) {
        if (mSDLThread == null || !mSDLThread.isAlive()) {
            return;
        }
        final long start = System.nanoTime();
        try {
            while (!nativeGamePaused()) {
                final long waited = (System.nanoTime() - start) / 1000000;
                if (waited >= timeoutMs) {
                    Log.w(GAME_TAG, "lifecycle: the game did not reach its pause in " + waited + " ms");
                    return;
                }
                Thread.sleep(5);
            }
        } catch (UnsatisfiedLinkError | InterruptedException e) {
            return;
        }
        final long waited = (System.nanoTime() - start) / 1000000;
        if (waited >= 100) {
            Log.i(GAME_TAG, "lifecycle: the surface waited " + waited + " ms for the game to reach its pause");
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
     * Called by the game (JNI, screen2_android.cpp) with [screen2] mode: the
     * second screen's panels on a second display, or not.
     */
    public static void setLowerScreenWanted(boolean wanted) {
        LowerScreen.setWanted(wanted);
    }

    /**
     * Called by the game (JNI, platform_android.cpp) after it started SDL's
     * text input again with another kind (a password field after a text
     * field): the keyboard keeps the text field's kind until its input is
     * restarted. Runs after SDL's own show task (the same main looper).
     *
     * SDL's stop and start of the text input (a hide right before the show)
     * can leave the keyboard away when the kind changes while it is up (seen
     * on the emulator: a text field to a password field by Tab, and back), and
     * a keyboard of another height can cover the field: twice after the
     * restart, while SDL's text edit has the focus, the keyboard is shown
     * again when it is not on the screen and the field's place (the text
     * edit, at the field's area) asks to be on the screen, so Android pans
     * the picture for the keyboard as it is now.
     */
    public static void restartTextInput() {
        final SDLActivity activity = mSingleton;
        if (activity instanceof DW4Activity) {
            final Handler handler = ((DW4Activity) activity).mMainHandler;
            handler.post(() -> {
                InputMethodManager imm = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
                if (imm != null && mTextEdit != null) {
                    imm.restartInput(mTextEdit);
                }
            });
            for (long delay : new long[] {150, 400}) {
                handler.postDelayed(() -> keepTextFieldInSight(activity), delay);
            }
        }
    }

    /** The keyboard up and the text field's place above it (see restartTextInput). */
    private static void keepTextFieldInSight(Context context) {
        final View edit = mTextEdit;
        if (edit == null || !edit.isShown() || !edit.hasFocus()) {
            return; // (the field was left meanwhile: SDL's hide gives the focus back to its surface)
        }
        WindowInsets insets = edit.getRootWindowInsets();
        if (insets != null && !insets.isVisible(WindowInsets.Type.ime())) {
            InputMethodManager imm = (InputMethodManager) context.getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) {
                imm.showSoftInput(edit, 0);
            }
        }
        edit.requestRectangleOnScreen(new Rect(0, 0, edit.getWidth(), edit.getHeight()), true);
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
    protected void onStart() {
        super.onStart();
        if (mLowerScreen != null) {
            mLowerScreen.start();
        }
    }

    /**
     * SDL pauses the game in onStop (Android 7+); the second screen's
     * surface goes before that (LowerScreen.stop: its end waits for a frame
     * drawing into it), so the game's thread never meets it released.
     */
    @Override
    protected void onStop() {
        if (mLowerScreen != null) {
            mLowerScreen.stop();
        }
        super.onStop();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            scheduleImmersive(0);
        } else if (mLowerScreen != null) {
            mLowerScreen.focusLost();
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
     * Only the RUNNING_* levels (memory is short while the game runs) reach
     * the game. BACKGROUND, MODERATE and COMPLETE come only while the app is
     * in the background, where the game loop stands in SDL's pause: the game
     * would let its textures go only after coming back (the ones it is about
     * to draw), never while the low memory killer decides.
     */
    @Override
    public void onTrimMemory(int level) {
        if (level >= ComponentCallbacks2.TRIM_MEMORY_UI_HIDDEN) {
            Log.i(GAME_TAG, "lifecycle: trim memory level " + level + " ("
                    + (level == ComponentCallbacks2.TRIM_MEMORY_UI_HIDDEN ? "the window is hidden" : "in the background")
                    + "; not passed on)");
            return;
        }
        Log.i(GAME_TAG, "lifecycle: trim memory level " + level + " (passed on as low memory)");
        super.onTrimMemory(level);
    }

    /**
     * The AYN Thor's controller has a Back button (KEY_BACK) besides Select
     * (BTN_SELECT), and SDL makes both its gamepad button BACK, so the Back
     * button would open the F1 menu only while no binding uses BACK. From
     * the Thor's controller KEYCODE_BACK is Android's Back key instead (the
     * F1 menu, always); Select stays the pad's BACK button.
     */
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK && isThorController(event.getDevice())) {
            if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) {
                SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_BACK);
            } else if (event.getAction() == KeyEvent.ACTION_UP) {
                SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_BACK);
            }
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    /** The AYN Thor's built-in controller ("Odin Controller", USB 2020:0111; not AYN's virtual mouse). */
    static boolean isThorController(InputDevice device) {
        if (device == null || (device.getSources() & InputDevice.SOURCE_GAMEPAD) != InputDevice.SOURCE_GAMEPAD) {
            return false;
        }
        return (device.getVendorId() == 0x2020 && device.getProductId() == 0x0111)
                || device.getName().contains("Odin Controller");
    }

    @Override
    protected String[] getLibraries() {
        // SDLActivity loads these in order and calls SDL_main in the last one.
        return new String[] {"SDL3", "main"};
    }

    /**
     * Command line arguments: adb shell am start ... --es dw4args "--seconds 10".
     * The launcher activity is exported, so any app on the device could start
     * it with arguments (--import-disc, --env, --card-dir, a save state). The
     * extra counts only in a debuggable build and only while the file
     * dw4args.allow is in the app's internal folder, which no other app can
     * write: adb shell run-as org.dw4pc.game touch files/dw4args.allow (and
     * rm to close it again). Otherwise it is ignored with a log line.
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
        if (!new File(getFilesDir(), "dw4args.allow").exists()) {
            Log.w(TAG, "dw4args ignored: no files/dw4args.allow (adb shell run-as org.dw4pc.game touch files/dw4args.allow)");
            return new String[0];
        }
        return args.trim().split("\\s+");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        scheduleImmersive(0);
        mLowerScreen = new LowerScreen(this);
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
        if (mLowerScreen != null) {
            mLowerScreen.release();
            mLowerScreen = null;
        }
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
