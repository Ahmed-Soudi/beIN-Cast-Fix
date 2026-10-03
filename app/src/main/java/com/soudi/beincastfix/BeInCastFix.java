package com.soudi.beincastfix;

import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

/**
 * v2-inert diagnostic build.
 *
 * Deliberately performs no logging, reflection, class lookup, or hooks.
 * The only purpose is to determine whether loading an Xposed module into
 * ptv.bein.mena is itself sufficient to reproduce the splash-screen crash.
 */
public final class BeInCastFix implements IXposedHookLoadPackage {
    @Override
    public void handleLoadPackage(XC_LoadPackage.LoadPackageParam lpparam) {
        if (!"ptv.bein.mena".equals(lpparam.packageName)) {
            return;
        }

        // Intentionally empty.
    }
}
