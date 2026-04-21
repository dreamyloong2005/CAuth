plugins {
    id("com.android.application")
    kotlin("android")
    kotlin("plugin.compose")
}

import org.jetbrains.kotlin.gradle.dsl.JvmTarget

android {
    namespace = "com.cauth.example"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.cauth.example"
        minSdk = 26
        targetSdk = 35
        versionCode = 4
        versionName = "0.3.1"
    }

    buildFeatures {
        compose = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlin {
        compilerOptions {
            jvmTarget.set(JvmTarget.JVM_17)
        }
    }
}

dependencies {
    implementation(project(":cauth-android-core"))
    implementation(project(":cauth-android-steam-auth"))
    implementation(project(":cauth-android-steam-depot"))
    implementation(project(":cauth-android-steam-cloud"))
    implementation(project(":cauth-android-compose"))

    implementation(platform("androidx.compose:compose-bom:2025.04.01"))
    implementation("androidx.activity:activity-compose:1.10.1")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.ui:ui-tooling-preview")

    debugImplementation("androidx.compose.ui:ui-tooling")
}
