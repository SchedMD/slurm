
# JAVA_HOME = /pkgs/lang/java/jdk117_v3
# JAVA_HOME = /pkgs/lang/java/jdk1.3.1
# JAVA_HOME = /pkgs/lang/java/j2sdk1.4.2
JAVA_HOME = /soft/com/packages/j2sdk1.4.2_04
SHELL     = /bin/sh
JC        = $(JAVA_HOME)/bin/javac
JVM       = $(JAVA_HOME)/bin/java
JFLAGS    =
JAR       = $(JAVA_HOME)/bin/jar
RM        = /bin/rm -f

JSRC      = simple.java simple_swing.java
JCLS      = $(JSRC:.java=.class)
JJAR      = $(JCLS:.class=.jar)

.SUFFIXES:      .java .class .jar

.java.class:
	$(JC) $(JFLAGS) $<

.class.jar:
	$(RM) simple.manifest ; \
	echo "Main-Class: $*" > simple.manifest ; \
	$(JAR) cvfm $@ simple.manifest $< ; \
	$(RM) simple.manifest

ALL: $(JCLS) $(JJAR) test_class test_jar

test_class:
	@echo "Testing class file" ; \
	 for class in *.class ; do \
	    if [ -f $$class ] ; then \
	        classname="`echo $$class|sed -e 's%\(.*\).class%\1%'`" ; \
	        $(RM) $$classname.log ; \
	        cmd='$(JVM) $$classname > $$classname.log 2>&1' ; \
	        if eval $$cmd ; then \
	            echo "	$$class is working" ; \
	        else \
	            echo "	$$class is NOT working" ; \
	        fi ; \
	    fi ; \
	 done

test_jar:
	@echo "Testing jar file" ; \
	 for jar in *.jar ; do \
	    if [ -f $$jar ] ; then \
	        $(RM) $$jar.log ; \
	        cmd='$(JVM) -jar $$jar > $$jar.log 2>&1' ; \
	        if eval $$cmd ; then \
	            echo "	$$jar is working" ; \
	        else \
	            echo "	$$jar is NOT working" ; \
	        fi ; \
	    fi ; \
	 done

clean:
	$(RM) *.manifest *.class *.jar

