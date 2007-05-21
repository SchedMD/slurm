import javax.swing.*;

/*
   Using JPanel instead of JFrame allows most Java2's JRE to run this code
   successfully without requiring DISPLAY set except jdk1.2.x.
   Apparently the instantiation of JFrame class checks for DISPLAY.

/sandbox/chan/slog2/build> ~/bin/swingtest
DISPLAY=
/sandbox/chan/java/jdk1.5.0_01/bin/java understands Swing.
/sandbox/chan/java/jdk1.5.0_01/bin/java works with Swing jar file
/sandbox/chan/java/j2sdk1.4.2/bin/java understands Swing.
/sandbox/chan/java/j2sdk1.4.2/bin/java works with Swing jar file
/soft/com/packages/j2sdk1.4.0_01/bin/java understands Swing.
/soft/com/packages/j2sdk1.4.0_01/bin/java works with Swing jar file
/soft/com/packages/jdk1.3.1_06/bin/java understands Swing.
/soft/com/packages/jdk1.3.1_06/bin/java works with Swing jar file
/soft/com/packages/maple-8.0/jre.IBM_INTEL_LINUX/bin/java understands Swing.
/soft/com/packages/maple-8.0/jre.IBM_INTEL_LINUX/bin/java works with Swing jar file
/sandbox/chan/java/jdk1.2.2/bin/java does NOT understand with Swing.
/sandbox/chan/java/jdk1.2.2/bin/java does NOT work with Swing jar file.
/sandbox/chan/java/jdk1.2/bin/java does NOT understand with Swing.
/sandbox/chan/java/jdk1.2/bin/java does NOT work with Swing jar file.
/sandbox/chan/java/jdk117_v3/bin/java does NOT understand with Swing.
/sandbox/chan/java/jdk117_v3/bin/java does NOT work with Swing jar file.

/sandbox/chan/slog2/build> ~/bin/swingtest
DISPLAY=localhost:14.0
/sandbox/chan/java/jdk1.5.0_01/bin/java understands Swing.
/sandbox/chan/java/jdk1.5.0_01/bin/java works with Swing jar file
/sandbox/chan/java/j2sdk1.4.2/bin/java understands Swing.
/sandbox/chan/java/j2sdk1.4.2/bin/java works with Swing jar file
/soft/com/packages/j2sdk1.4.0_01/bin/java understands Swing.
/soft/com/packages/j2sdk1.4.0_01/bin/java works with Swing jar file
/soft/com/packages/jdk1.3.1_06/bin/java understands Swing.
/soft/com/packages/jdk1.3.1_06/bin/java works with Swing jar file
/soft/com/packages/maple-8.0/jre.IBM_INTEL_LINUX/bin/java understands Swing.
/soft/com/packages/maple-8.0/jre.IBM_INTEL_LINUX/bin/java works with Swing jar file
/sandbox/chan/java/jdk1.2.2/bin/java understands Swing.
/sandbox/chan/java/jdk1.2.2/bin/java works with Swing jar file
/sandbox/chan/java/jdk1.2/bin/java understands Swing.
/sandbox/chan/java/jdk1.2/bin/java works with Swing jar file
/sandbox/chan/java/jdk117_v3/bin/java does NOT understand with Swing.
/sandbox/chan/java/jdk117_v3/bin/java does NOT work with Swing jar file.

*/
public class simple_swing
{
    public static void main( String args[] )
    {
        // JFrame frame = new JFrame();
        // JPanel panel = new JPanel();
        JPanel panel;
        System.exit( 0 );
    }
}
