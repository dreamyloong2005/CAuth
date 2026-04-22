package com.cauth.android

enum class CAuthFileWriteMode(val nativeValue: Int) {
    Overwrite(0),
    SkipExisting(1),
    FailIfExists(2),
}

data class CAuthFileWriteOptions(
    val mode: CAuthFileWriteMode = CAuthFileWriteMode.Overwrite,
    val atomicWrite: Boolean = true,
)
