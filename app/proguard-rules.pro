# Add project specific ProGuard rules here.
# You can control the set of applied configuration files using the
# proguardFiles setting in build.gradle.
#
# For more details, see
#   http://developer.android.com/guide/developing/tools/proguard.html

# If your project uses WebView with JS, uncomment the following
# and specify the fully qualified class name to the JavaScript interface
# class:
#-keepclassmembers class fqcn.of.javascript.interface.for.webview {
#   public *;
#}

# Uncomment this to preserve the line number information for
# debugging stack traces.
#-keepattributes SourceFile,LineNumberTable

# If you keep the line number information, uncomment this to
# hide the original source file name.
#-renamesourcefileattribute SourceFile

# Keep classes and methods called from native code (JNI)
-keepclasseswithmembernames,includedescriptorclasses class * {
    native <methods>;
}

# Keep all skymodloader classes that might be accessed from native code
-keep class git.artdeell.skymodloader.** { *; }

# Keep all TGC Sky classes (game classes accessed by native code)
-keep class com.tgc.sky.** { *; }

# Keep FileSelector class and its methods (called from native code)
-keep class git.artdeell.skymodloader.FileSelector {
    *;
}

# Keep MainActivity and all its methods (called from native code)
-keep class git.artdeell.skymodloader.MainActivity {
    *;
}

# Keep DialogJNI and its methods (called from native code)
-keep class git.artdeell.skymodloader.DialogJNI {
    *;
}

# Keep ImGUI and its methods (called from native code)
-keep class git.artdeell.skymodloader.ImGUI {
    *;
}

# Keep LibrarySelectorListener and its methods (called from native code)
-keep class git.artdeell.skymodloader.LibrarySelectorListener {
    *;
}

# Keep iconloader classes (called from native code)
-keep class git.artdeell.skymodloader.iconloader.** {
    *;
}

# Keep all FMOD classes (used by native libraries)
-keep class org.fmod.** { *; }

# Keep DeviceInfo class (used by MainActivity)
-keep class git.artdeell.skymodloader.DeviceInfo {
    *;
}

# Keep ElfLoader classes (used by native library loading)
-keep class git.artdeell.skymodloader.elfmod.ElfLoader {
    *;
}
-keep class git.artdeell.skymodloader.elfmod.ElfRefcountLoader {
    *;
}

# Keep all attributes for reflection
-keepattributes *Annotation*,Signature,Exception,InnerClasses,EnclosingMethod

# Keep BuildConfig (used throughout the app)
-keep class git.artdeell.skymodloader.BuildConfig { *; }
-keep class com.tgc.sky.BuildConfig { *; }

# Not fully optimized, but better than nothing:

# Keep JElf library (used by ElfLoader for ELF file parsing)
-keep class net.fornwall.jelf.** { *; }

# Keep ElfLoader class (used in MainActivity)
-keep class git.artdeell.skymodloader.ElfLoader { *; }

# Keep SMLApplication (stores resources)
-keep class git.artdeell.skymodloader.SMLApplication { *; }

# Keep GameActivity (started by MainActivity)
-keep class com.tgc.sky.GameActivity { *; }

# Keep TGCNativeActivity (parent of GameActivity)
-keep class com.tgc.sky.TGCNativeActivity { *; }

# Keep ArrayMap (used in ElfLoader)
-keep class androidx.collection.ArrayMap { *; }

# Keep all inner classes
-keepattributes InnerClasses
-keep class **$* { *; }

# Keep enum methods
-keepclassmembers enum * {
    public static **[] values();
    public static ** valueOf(java.lang.String);
    **[] $VALUES;
    public *;
}

# Preserve line numbers for debugging stack traces
-keepattributes SourceFile,LineNumberTable
-renamesourcefileattribute SourceFile

# Keep all public classes/methods in git.artdeell and com.tgc packages
-keepclassmembers class git.artdeell.** {
    public *;
}
-keepclassmembers class com.tgc.** {
    public *;
}

# Keep fields in classes that interact with native code
-keepclassmembers class git.artdeell.skymodloader.DeviceInfo {
    <fields>;
}

# Don't obfuscate classes that are loaded dynamically
-keepnames class git.artdeell.skymodloader.**
-keepnames class com.tgc.sky.**

# Don't warn about missing Sky APK classes
-dontwarn com.tgc.**

# Don't warn about missing reflection classes
-dontwarn java.lang.reflect.AnnotatedType