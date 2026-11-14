package org.dw4pc.game;

import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/**
 * The second screen (phase E, docs/ANDROID.md): the AYN Thor's lower screen,
 * or any second display Android offers for presentations. A DisplayListener
 * finds the display (FLAG_PRESENTATION, not private, not the game's own
 * display, not off; the lowest id when there are several, so the built-in
 * screen before a later video out), and a Presentation there hosts a
 * SurfaceView whose surface the game draws its panels into
 * (src/game/screen2_android.cpp) and whose touches it takes.
 *
 * The Presentation's window is neither focusable nor touch modal: a touch on
 * it never takes the input focus (and with it the controller) from the game
 * on the top screen. It goes before SDL's pause (the activity's onStop, its
 * surface released first) and comes again with onStart; without such a
 * display (none, switched off, the game itself on it) there is none, and the
 * game shows the same panels as an overlay instead.
 */
final class LowerScreen implements DisplayManager.DisplayListener {
    private static final String TAG = "DW4";

    private final Activity mActivity;
    private final DisplayManager mDisplays;
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private Panels mPanels;
    private boolean mStarted;
    private String mReported = "";

    /** [screen2] mode as the game asks for it (any thread). */
    private static volatile boolean sWanted = true;
    /** The running activity's (main thread). */
    private static LowerScreen sCurrent;

    static native void nativeSurface(Surface surface);
    static native boolean nativeSurfaceGone();
    static native void nativeDisplay(int id, String name, int width, int height, int dpi);
    static native void nativeTouch(int action, float x, float y);

    LowerScreen(Activity activity) {
        mActivity = activity;
        mDisplays = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
        if (mDisplays != null) {
            mDisplays.registerDisplayListener(this, mHandler);
        }
        sCurrent = this;
    }

    /** Called by the game (JNI, through DW4Activity) when [screen2] mode changes. */
    static void setWanted(boolean wanted) {
        sWanted = wanted;
        new Handler(Looper.getMainLooper()).post(() -> {
            if (sCurrent != null) {
                sCurrent.update(wanted ? "the game's setting is on" : "the game's setting is off");
            }
        });
    }

    void start() {
        mStarted = true;
        update("the activity is visible");
    }

    /** Before SDL's pause: the surface goes first (surfaceDestroyed waits for a frame drawing into it). */
    void stop() {
        mStarted = false;
        dismiss("the activity is no longer visible");
    }

    void release() {
        stop();
        if (mDisplays != null) {
            mDisplays.unregisterDisplayListener(this);
        }
        if (sCurrent == this) {
            sCurrent = null;
        }
    }

    private Display pick() {
        if (mDisplays == null) {
            return null;
        }
        Display own = mActivity.getDisplay();
        int ownId = own != null ? own.getDisplayId() : Display.DEFAULT_DISPLAY;
        Display best = null;
        for (Display d : mDisplays.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            int flags = d.getFlags();
            if (d.getDisplayId() == ownId || (flags & Display.FLAG_PRESENTATION) == 0 || (flags & Display.FLAG_PRIVATE) != 0
                    || d.getState() == Display.STATE_OFF || !d.isValid()) {
                continue;
            }
            if (best == null || d.getDisplayId() < best.getDisplayId()) {
                best = d;
            }
        }
        return best;
    }

    private void update(String why) {
        Display found = pick();
        report(found);
        Display target = mStarted && sWanted ? found : null;
        if (mPanels != null && (target == null || mPanels.getDisplay().getDisplayId() != target.getDisplayId())) {
            dismiss(why);
        }
        if (target != null && mPanels == null) {
            show(target, why);
        }
    }

    /** The display for F1 > Settings (id -1: none), logged when it changes. */
    private void report(Display d) {
        String line;
        try {
            if (d == null) {
                line = "none";
                nativeDisplay(-1, "", 0, 0, 0);
            } else {
                DisplayMetrics metrics = new DisplayMetrics();
                d.getRealMetrics(metrics);
                line = "display " + d.getDisplayId() + " '" + d.getName() + "' " + metrics.widthPixels + "x" + metrics.heightPixels
                        + " " + metrics.densityDpi + " dpi, flags 0x" + Integer.toHexString(d.getFlags());
                nativeDisplay(d.getDisplayId(), d.getName(), metrics.widthPixels, metrics.heightPixels, metrics.densityDpi);
            }
        } catch (UnsatisfiedLinkError e) {
            return;
        }
        if (!line.equals(mReported)) {
            mReported = line;
            Log.i(TAG, "second screen: " + line);
        }
    }

    private void show(Display display, String why) {
        Panels panels = new Panels(mActivity, display);
        panels.setOnDismissListener(dialog -> {
            if (mPanels == dialog) {
                mPanels = null;
            }
        });
        try {
            panels.show();
        } catch (WindowManager.InvalidDisplayException e) {
            Log.w(TAG, "second screen: display " + display.getDisplayId() + " refused the panels: " + e.getMessage());
            return;
        }
        mPanels = panels;
        Log.i(TAG, "second screen: shown on display " + display.getDisplayId() + " (" + why + ")");
    }

    private void dismiss(String why) {
        Panels panels = mPanels;
        if (panels == null) {
            return;
        }
        mPanels = null;
        panels.dismiss();
        Log.i(TAG, "second screen: gone (" + why + ")");
    }

    @Override
    public void onDisplayAdded(int displayId) {
        update("display " + displayId + " added");
    }

    @Override
    public void onDisplayRemoved(int displayId) {
        update("display " + displayId + " removed");
    }

    @Override
    public void onDisplayChanged(int displayId) {
        update("display " + displayId + " changed");
    }

    /** The Presentation: a SurfaceView filling the display, not focusable, its touches to the game. */
    static final class Panels extends Presentation implements SurfaceHolder.Callback {
        Panels(Context outerContext, Display display) {
            super(outerContext, display);
        }

        @Override
        protected void onCreate(Bundle savedInstanceState) {
            super.onCreate(savedInstanceState);
            Window window = getWindow();
            window.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL);
            SurfaceView view = new SurfaceView(getContext());
            view.getHolder().addCallback(this);
            view.setOnTouchListener((v, event) -> {
                touch(event);
                return true;
            });
            setContentView(view);
            // The whole display, without bars where it would have them.
            WindowManager.LayoutParams attributes = window.getAttributes();
            attributes.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
            window.setAttributes(attributes);
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                controller.hide(WindowInsets.Type.systemBars());
            }
        }

        private static void touch(MotionEvent event) {
            int action;
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN: action = 0; break;
                case MotionEvent.ACTION_MOVE: action = 1; break;
                case MotionEvent.ACTION_UP: action = 2; break;
                case MotionEvent.ACTION_CANCEL: action = 3; break;
                default: return; // (a second finger: the panels take the first one only)
            }
            try {
                nativeTouch(action, event.getX(0), event.getY(0));
            } catch (UnsatisfiedLinkError e) {
                // (no game library)
            }
        }

        @Override
        public void surfaceCreated(SurfaceHolder holder) {
        }

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            Log.i(TAG, "second screen: surface " + width + "x" + height + " on display " + getDisplay().getDisplayId());
            try {
                nativeSurface(holder.getSurface());
            } catch (UnsatisfiedLinkError e) {
                Log.w(TAG, "second screen: no game library: " + e);
            }
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            final long start = System.nanoTime();
            boolean released = true;
            try {
                released = nativeSurfaceGone();
            } catch (UnsatisfiedLinkError e) {
                // (no game library)
            }
            final long waited = (System.nanoTime() - start) / 1000000;
            Log.i(TAG, "second screen: surface released" + (released ? "" : " by the game's frame after it") + " (" + waited + " ms)");
        }
    }
}
