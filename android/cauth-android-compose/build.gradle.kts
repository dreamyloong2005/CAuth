plugins {
    id("com.android.library")
    kotlin("android")
    kotlin("plugin.compose")
}

import org.jetbrains.kotlin.gradle.dsl.JvmTarget

android {
    namespace = "com.cauth.android.compose"
    compileSdk = 35

    defaultConfig {
        minSdk = 26
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

    implementation(platform("androidx.compose:compose-bom:2025.04.01"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")

    debugImplementation("androidx.compose.ui:ui-tooling")
}
