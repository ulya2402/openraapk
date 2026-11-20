using Android.App;
using Android.Runtime;
using Java.Interop;

namespace OpenRA.Android
{
    // Exposes the C# entry pointer to Java's LauncherActivity / OpenRAGameActivity
    [Register("org/openra/android/DotNetBridge")]
    public sealed class DotNetBridge : Java.Lang.Object
    {
        [Export("getOpenRAEntryPointer")]
        public static long GetOpenRAEntryPointer()
        {
            return AndroidEntryRegistry.EntryPointer != 0
                ? AndroidEntryRegistry.EntryPointer
                : MainApplication.GetNativeEntryPointer();
        }
    }
}
