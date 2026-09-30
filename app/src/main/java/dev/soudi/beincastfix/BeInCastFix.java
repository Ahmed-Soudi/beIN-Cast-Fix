package dev.soudi.beincastfix;

import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

public final class BeInCastFix implements IXposedHookLoadPackage {
    private static final String TARGET_PACKAGE = "ptv.bein.mena";

    @Override
    public void handleLoadPackage(XC_LoadPackage.LoadPackageParam lpparam) {
        if (!TARGET_PACKAGE.equals(lpparam.packageName)) {
            return;
        }

        // v2-inert diagnostic:
        // Intentionally do nothing inside beIN CONNECT MENA.
        // No logging, reflection, class lookup, or hooks.
    }
}
