// Off Android, each JNI bridge package (android/*/src/main/cpp/mcpp.toml) builds
// as a static library holding only this translation unit, so a desktop
// `mcpp build` of the workspace has something to link and skips the JNI code.
namespace cauth::android_jni_desktop_stub {
} // namespace cauth::android_jni_desktop_stub
