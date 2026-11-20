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

        [DllImport("launcher_jni", CallingConvention = CallingConvention.Cdecl)]
        static extern void openra_set_entry_ptr(long ptr);

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
            long ptr = GetNativeEntryPointer();
            AndroidEntryRegistry.EntryPointer = ptr;
            openra_set_entry_ptr(ptr);
            Log.Info(Tag, $"Registered OpenRA C# entry pointer: 0x{ptr:X}");
        }
    }

    internal static class AndroidEntryRegistry
    {
        public static long EntryPointer { get; set; }
    }
}
