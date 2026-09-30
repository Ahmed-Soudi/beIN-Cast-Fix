package dev.soudi.beincastfix;

import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

public final class BeInCastFix implements IXposedHookLoadPackage {
    @Override
    public void handleLoadPackage(XC_LoadPackage.LoadPackageParam lpparam) {
        // Keep scope narrow in LSPosed: select only the beIN CONNECT MENA package.
        // Hook implementation will be added after mapping the 10.3.7 -> 10.4 routing change.
        XposedBridge.log("beIN-Cast-Fix loaded in " + lpparam.packageName);
    }
}
