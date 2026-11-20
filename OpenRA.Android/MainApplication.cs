using System;
using System.Runtime.InteropServices;
using Android.App;
using Android.Runtime;
using Android.Util;

namespace OpenRA.Android
{
    [Application(Name = "org.openra.android.MainApplication")]
    public sealed class MainApplication : Application
    {
        const string Tag = "OpenRA.Android";

        public MainApplication(IntPtr javaReference, JniHandleOwnership transfer)
            : base(javaReference, transfer)
        {
        }

        [DllImport("launcher_jni", EntryPoint = "Java_org_openra_android_NativeBridge_nativeConfigureLaunch")]
        static extern void NativeConfigureLaunch(
            IntPtr env, IntPtr thiz, long entryFnPtr, IntPtr jEngineDir, IntPtr jSupportDir, IntPtr jModId);

        [UnmanagedCallersOnly]
        static unsafe int RunOpenRAEngine(byte* engineDirPtr, byte* supportDirPtr, byte* modIdPtr)
        {
            var engineDir = Marshal.PtrToStringUTF8((IntPtr)engineDirPtr) ?? "";
            var supportDir = Marshal.PtrToStringUTF8((IntPtr)supportDirPtr) ?? "";
            var modId = Marshal.PtrToStringUTF8((IntPtr)modIdPtr) ?? "ra";

            try
            {
                Log.Info(Tag, $"Initializing OpenRA (mod={modId}, engineDir={engineDir}, supportDir={supportDir})");
                var args = new[]
                {
                    $"Engine.EngineDir={engineDir}",
                    $"Engine.SupportDir={supportDir}",
                    $"Game.Mod={modId}"
                };

                return (int)Game.InitializeAndRun(args);
            }
            catch (Exception ex)
            {
                Log.Error(Tag, $"Fatal OpenRA error: {ex}");
                return 1;
            }
            finally
            {
                Log.Dispose();
            }
        }

        public static unsafe long GetNativeEntryPointer()
        {
            delegate* unmanaged<byte*, byte*, byte*, int> fnPtr = &RunOpenRAEngine;
            return (long)(IntPtr)fnPtr;
        }

        public override void OnCreate()
        {
            base.OnCreate();
            Java.Lang.JavaSystem.LoadLibrary("launcher_jni");
            AndroidEntryRegistry.EntryPointer = GetNativeEntryPointer();
            Log.Info(Tag, $"Registered OpenRA C# entry pointer: 0x{AndroidEntryRegistry.EntryPointer:X}");
        }
    }

    internal static class AndroidEntryRegistry
    {
        public static long EntryPointer { get; set; }
    }
}
