package io.github.liltbsp.dualdock;

import android.app.Presentation;
import android.content.Context;
import android.os.Bundle;
import android.view.Display;
import android.view.WindowManager;

/**
 * Shows the dock on the second (bottom) display.
 *
 * The window is marked not focusable, so touching it never takes keyboard /
 * controller focus away from the game on the top screen. Touches still arrive.
 */
public class DockPresentation extends Presentation {

    private final DockView dock;

    public DockPresentation(Context outerContext, Display display, int buildNumber) {
        super(outerContext, display);
        dock = new DockView(getContext(), buildNumber);
    }

    public DockView getDock() {
        return dock;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
        setContentView(dock);
    }
}
