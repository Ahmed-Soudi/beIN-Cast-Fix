package dev.soudi.beincastfix;

import io.github.libxposed.api.XposedModule;
import io.github.libxposed.api.XposedModuleInterface;

/**
 * Modern libxposed API 102 entry point.
 *
 * This revision is deliberately inert. Its only purpose is to verify that beIN can
 * start with the modern module loader before we add any Cast-specific interception.
 */
public final class BeInCastFix extends XposedModule {
    private static final String TARGET_PACKAGE = "ptv.bein.mena";

    public BeInCastFix() {
        super();
    }

    @Override
    public void onPackageReady(XposedModuleInterface.PackageReadyParam param) {
        if (!TARGET_PACKAGE.equals(param.getPackageName()) || !param.isFirstPackage()) {
            return;
        }

        // v3 modern-loader smoke test: intentionally no reflection, hooks, or logging.
    }
}
