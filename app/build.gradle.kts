plugins { id("com.android.application") }

android {
    namespace = "dev.soudi.beincastfix"
    compileSdk = 35

    defaultConfig {
        applicationId = "dev.soudi.beincastfix"
        minSdk = 28
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"
    }
}

dependencies {
    compileOnly("de.robv.android.xposed:api:82")
}
