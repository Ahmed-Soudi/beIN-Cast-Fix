import android.content.Context;
import android.util.Log;
import com.soudi.beincastroot.HookBridge;

public final class CallbackTest {
    public static final class TestContext extends Context {
        private final String packageName;
        public TestContext(String packageName) { this.packageName = packageName; }
        public String getPackageName() { return packageName; }
        public ClassLoader getClassLoader() { return CallbackTest.class.getClassLoader(); }
    }
    public static final class AttachTarget {
        int calls;
        Context attachedContext;
        final Object result = new Object();
        Throwable failure;
        public Object attach(Context context) throws Throwable {
            calls++;
            attachedContext = context;
            if (failure != null) throw failure;
            return result;
        }
    }
    public static final class BuilderTarget {
        boolean switcher = true;
        int calls;
        final String receiverId = "unchanged-receiver";
        public BuilderTarget setter(boolean enabled) { switcher = enabled; calls++; return this; }
    }
    private static void require(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }
    public static void main(String[] arguments) throws Throwable {
        HookBridge attach = new HookBridge(0);
        attach.backup = AttachTarget.class.getMethod("attach", Context.class);
        AttachTarget target = new AttachTarget();
        Context context = new TestContext("another.package");
        Object result = attach.callback(new Object[] {target, context});
        require(result == target.result && target.calls == 1 && target.attachedContext == context,
                "Original attach behavior was not preserved");

        Throwable originalFailure = new IllegalArgumentException("original app failure");
        target.failure = originalFailure;
        try {
            attach.callback(new Object[] {target, context});
            throw new AssertionError("Original attach failure was swallowed");
        } catch (Throwable actual) {
            require(actual == originalFailure, "Original exception was replaced or wrapped");
        }

        target.failure = null;
        Context beinContext = new TestContext("ptv.bein.mena");
        // Native registration intentionally absent in the host test. A failed
        // Cast setup must leave a completed application attach successful.
        int errorsBefore = Log.errors;
        result = attach.callback(new Object[] {target, beinContext});
        require(result == target.result && target.attachedContext == beinContext && Log.errors == errorsBefore + 1,
                "Failed Cast installation changed a successful attach");

        HookBridge setter = new HookBridge(1);
        setter.backup = BuilderTarget.class.getMethod("setter", boolean.class);
        BuilderTarget builder = new BuilderTarget();
        Object[] requested = new Object[] {builder, Boolean.TRUE};
        result = setter.callback(requested);
        require(result == builder && !builder.switcher && builder.calls == 1,
                "Cast setter did not invoke the original with false");
        require(requested[1] == Boolean.TRUE && "unchanged-receiver".equals(builder.receiverId),
                "Cast override changed unrelated input or state");
        System.out.println("Callback checks passed (host only; ART and casting untested)");
    }
}
