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

# Keep MainActivity native methods
-keep class git.artdeell.skymodloader.MainActivity {
    *;
}

# Keep all FMOD classes (used by native libraries)
-keep class org.fmod.** { *; }

# Keep all attributes for reflection
-keepattributes *Annotation*,Signature,Exception,InnerClasses,EnclosingMethod