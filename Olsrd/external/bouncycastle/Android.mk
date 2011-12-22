#
# Copyright (C) 2010 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
LOCAL_PATH := $(call my-dir)

bouncycastle_src_files := $(call all-java-files-under,src/main/java)

include $(CLEAR_VARS)
LOCAL_MODULE := bouncycastle
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := $(bouncycastle_src_files)
LOCAL_JAVACFLAGS := -encoding UTF-8
LOCAL_JAVA_LIBRARIES := core
LOCAL_NO_STANDARD_LIBRARIES := true
include $(BUILD_JAVA_LIBRARY)

# This is used to generate a list of what is unused so it can be removed when bouncycastle is updated.
# Based on "Finding dead code" example in ProGuard manual at http://proguard.sourceforge.net/
.PHONY: bouncycastle-proguard-deadcode
bouncycastle-proguard-deadcode: $(full_classes_compiled_jar) $(full_java_libs)
	$(PROGUARD) \
		-injars $(full_classes_compiled_jar) \
		-libraryjars "$(call normalize-path-list,$(addsuffix (!org/bouncycastle/**.class,!org/apache/harmony/xnet/provider/jsse/OpenSSLMessageDigest.class),$(full_java_libs)))" \
		-dontoptimize \
		-dontobfuscate \
		-dontpreverify \
		-ignorewarnings \
		-printusage \
		-keep class org.bouncycastle.jce.provider.BouncyCastleProvider "{ public protected *; }" \
		-keep class org.bouncycastle.jce.provider.symmetric.AESMappings "{ public protected *; }" \
		-keep class org.bouncycastle.asn1.ASN1TaggedObject "{ public protected *; }" \
		-keep class org.bouncycastle.asn1.x509.CertificateList "{ public protected *; }" \
		-keep class org.bouncycastle.crypto.AsymmetricBlockCipher "{ public protected *; }" \
		-keep class org.bouncycastle.x509.ExtendedPKIXBuilderParameters "{ public protected *; }" \
		`(find $(LOCAL_PATH) -name '*.java' | xargs grep '"org.bouncycastle' | egrep '  (put|add)' | sed -e 's/");//' -e 's/.*"//'; \
		  find $(LOCAL_PATH) -name '*.java' | xargs grep '  addHMACAlgorithm' | sed 's/"org.bouncycastle/\norg.bouncycastle/g' | grep ^org.bouncycastle | sed 's/".*//'; \
                  find . -name '*.java' | xargs grep 'import org.bouncycastle' | grep -v /bouncycastle/ | sed -e 's/.*:import //' -e 's/;//') \
		  | sed -e 's/^/-keep class /' -e 's/$$/ { public protected \*; } /' | sort | uniq` \
		-keepclassmembers "class * { \
		    static final %                *; \
		    static final java.lang.String *; \
		}" \
		-keepclassmembers "class * implements java.io.Serializable { \
		    private static final java.io.ObjectStreamField[] serialPersistentFields; \
		    private void writeObject(java.io.ObjectOutputStream); \
		    private void readObject(java.io.ObjectInputStream); \
		    java.lang.Object writeReplace(); \
		    java.lang.Object readResolve(); \
		}" \
		-keepclassmembers "interface org.bouncycastle.crypto.paddings.BlockCipherPadding { \
		    abstract public java.lang.String getPaddingName(); \
		}" \
		-keepclassmembers "class * implements org.bouncycastle.crypto.paddings.BlockCipherPadding { \
		    public java.lang.String getPaddingName(); \
		}"

ifeq ($(WITH_HOST_DALVIK),true)
    include $(CLEAR_VARS)
    LOCAL_MODULE := bouncycastle-hostdex
    LOCAL_MODULE_TAGS := optional
    LOCAL_SRC_FILES := $(bouncycastle_src_files)
    LOCAL_JAVACFLAGS := -encoding UTF-8
    LOCAL_JAVA_LIBRARIES := core-hostdex
    LOCAL_NO_STANDARD_LIBRARIES := true
    LOCAL_BUILD_HOST_DEX := true
    LOCAL_MODULE_TAGS := optional
    include $(BUILD_HOST_JAVA_LIBRARY)
endif
