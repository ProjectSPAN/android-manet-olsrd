package org.bouncycastle.jce.provider;

import org.bouncycastle.crypto.CipherKeyGenerator;
import org.bouncycastle.crypto.KeyGenerationParameters;
import org.bouncycastle.crypto.generators.DESKeyGenerator;
import org.bouncycastle.crypto.generators.DESedeKeyGenerator;

import javax.crypto.KeyGeneratorSpi;
import javax.crypto.SecretKey;
import javax.crypto.spec.SecretKeySpec;
import java.security.InvalidAlgorithmParameterException;
import java.security.InvalidParameterException;
import java.security.SecureRandom;
import java.security.spec.AlgorithmParameterSpec;

public class JCEKeyGenerator
    extends KeyGeneratorSpi
{
    protected String                algName;
    protected int                   keySize;
    protected int                   defaultKeySize;
    protected CipherKeyGenerator    engine;

    protected boolean               uninitialised = true;

    protected JCEKeyGenerator(
        String              algName,
        int                 defaultKeySize,
        CipherKeyGenerator  engine)
    {
        this.algName = algName;
        this.keySize = this.defaultKeySize = defaultKeySize;
        this.engine = engine;
    }

    protected void engineInit(
        AlgorithmParameterSpec  params,
        SecureRandom            random)
    throws InvalidAlgorithmParameterException
    {
        throw new InvalidAlgorithmParameterException("Not Implemented");
    }

    protected void engineInit(
        SecureRandom    random)
    {
        if (random != null)
        {
            engine.init(new KeyGenerationParameters(random, defaultKeySize));
            uninitialised = false;
        }
    }

    protected void engineInit(
        int             keySize,
        SecureRandom    random)
    {
        try
        {
            engine.init(new KeyGenerationParameters(random, keySize));
            uninitialised = false;
        }
        catch (IllegalArgumentException e)
        {
            throw new InvalidParameterException(e.getMessage());
        }
    }

    protected SecretKey engineGenerateKey()
    {
        if (uninitialised)
        {
            engine.init(new KeyGenerationParameters(new SecureRandom(), defaultKeySize));
            uninitialised = false;
        }

        return new SecretKeySpec(engine.generateKey(), algName);
    }

    /**
     * the generators that are defined directly off us.
     */

    /**
     * DES
     */
    public static class DES
        extends JCEKeyGenerator
    {
        public DES()
        {
            super("DES", 64, new DESKeyGenerator());
        }
    }

    /**
     * DESede - the default for this is to generate a key in 
     * a-b-a format that's 24 bytes long but has 16 bytes of
     * key material (the first 8 bytes is repeated as the last
     * 8 bytes). If you give it a size, you'll get just what you
     * asked for.
     */
    public static class DESede
        extends JCEKeyGenerator
    {
        private boolean     keySizeSet = false;

        public DESede()
        {
            super("DESede", 192, new DESedeKeyGenerator());
        }

        protected void engineInit(
            int             keySize,
            SecureRandom    random)
        {
            super.engineInit(keySize, random);
            keySizeSet = true;
        }

        protected SecretKey engineGenerateKey()
        {
            if (uninitialised)
            {
                engine.init(new KeyGenerationParameters(new SecureRandom(), defaultKeySize));
                uninitialised = false;
            }

            //
            // if no key size has been defined generate a 24 byte key in
            // the a-b-a format
            //
            if (!keySizeSet)
            {
                byte[]     k = engine.generateKey();

                System.arraycopy(k, 0, k, 16, 8);

                return (SecretKey)(new SecretKeySpec(k, algName));
            }
            else
            {
                return (SecretKey)(new SecretKeySpec(engine.generateKey(), algName));
            }
        }
    }
    
    // BEGIN android-removed
    // /**
    //  * generate a desEDE key in the a-b-c format.
    //  */
    // public static class DESede3
    //     extends JCEKeyGenerator
    // {
    //     public DESede3()
    //     {
    //         super("DESede3", 192, new DESedeKeyGenerator());
    //     }
    // }
    //
    // /**
    //  * SKIPJACK
    //  */
    // public static class Skipjack
    //     extends JCEKeyGenerator
    // {
    //     public Skipjack()
    //     {
    //         super("SKIPJACK", 80, new CipherKeyGenerator());
    //     }
    // }
    // END android-removed
    
    /**
     * Blowfish
     */
    public static class Blowfish
        extends JCEKeyGenerator
    {
        public Blowfish()
        {
            super("Blowfish", 128, new CipherKeyGenerator());
        }
    }
    
    // BEGIN android-removed
    // /**
    //  * Twofish
    //  */
    // public static class Twofish
    //     extends JCEKeyGenerator
    // {
    //     public Twofish()
    //     {
    //         super("Twofish", 256, new CipherKeyGenerator());
    //     }
    // }
    //
    // /**
    //  * RC2
    //  */
    // public static class RC2
    //     extends JCEKeyGenerator
    // {
    //     public RC2()
    //     {
    //         super("RC2", 128, new CipherKeyGenerator());
    //     }
    // }
    // END android-removed
    
    /**
     * RC4
     */
    public static class RC4
        extends JCEKeyGenerator
    {
        public RC4()
        {
            super("RC4", 128, new CipherKeyGenerator());
        }
    }
    
    // BEGIN android-removed
    // /**
    //  * RC5
    //  */
    // public static class RC5
    //     extends JCEKeyGenerator
    // {
    //     public RC5()
    //     {
    //         super("RC5", 128, new CipherKeyGenerator());
    //     }
    // }
    //
    // /**
    //  * RC5
    //  */
    // public static class RC564
    //     extends JCEKeyGenerator
    // {
    //     public RC564()
    //     {
    //         super("RC5-64", 256, new CipherKeyGenerator());
    //     }
    // }
    //
    // /**
    //  * RC6
    //  */
    // public static class RC6
    //     extends JCEKeyGenerator
    // {
    //     public RC6()
    //     {
    //         super("RC6", 256, new CipherKeyGenerator());
    //     }
    // }
    //
    // /**
    //  * GOST28147
    //  */
    // public static class GOST28147
    //     extends JCEKeyGenerator
    // {
    //     public GOST28147()
    //     {
    //         super("GOST28147", 256, new CipherKeyGenerator());
    //     }
    // }
    
    // /**
    //  * Rijndael
    //  */
    // public static class Rijndael
    //     extends JCEKeyGenerator
    // {
    //     public Rijndael()
    //     {
    //         super("Rijndael", 192, new CipherKeyGenerator());
    //     }
    // }
    //
    // /**
    //  * Serpent
    //  */
    // public static class Serpent
    //     extends JCEKeyGenerator
    // {
    //     public Serpent()
    //     {
    //         super("Serpent", 192, new CipherKeyGenerator());
    //     }
    // }
    //
    //
    //
    // /**
    //  * CAST6
    //  */
    // public static class CAST6
    //     extends JCEKeyGenerator
    // {
    //     public CAST6()
    //     {
    //         super("CAST6", 256, new CipherKeyGenerator());
    //     }
    // }
    //
    // /**
    //  * TEA
    //  */
    // public static class TEA
    //     extends JCEKeyGenerator
    // {
    //     public TEA()
    //     {
    //         super("TEA", 128, new CipherKeyGenerator());
    //     }
    // }
    //
    // /**
    //  * XTEA
    //  */
    // public static class XTEA
    //     extends JCEKeyGenerator
    // {
    //     public XTEA()
    //     {
    //         super("XTEA", 128, new CipherKeyGenerator());
    //     }
    // }
    //
    // /**
    //  * Salsa20
    //  */
    // public static class Salsa20
    //     extends JCEKeyGenerator
    // {
    //     public Salsa20()
    //     {
    //         super("Salsa20", 128, new CipherKeyGenerator());
    //     }
    // }
    //
    // /**
    //  * HC128
    //  */
    // public static class HC128
    //     extends JCEKeyGenerator
    // {
    //     public HC128()
    //     {
    //         super("HC128", 128, new CipherKeyGenerator());
    //     }
    // }
    //
    // /**
    //  * HC256
    //  */
    // public static class HC256
    //     extends JCEKeyGenerator
    // {
    //     public HC256()
    //     {
    //         super("HC256", 256, new CipherKeyGenerator());
    //     }
    // }
    //
    // /**
    //  * VMPC
    //  */
    // public static class VMPC
    //     extends JCEKeyGenerator
    // {
    //     public VMPC()
    //     {
    //         super("VMPC", 128, new CipherKeyGenerator());
    //     }
    // }
    //
    // /**
    //  * VMPC-KSA3
    //  */
    // public static class VMPCKSA3
    //     extends JCEKeyGenerator
    // {
    //     public VMPCKSA3()
    //     {
    //         super("VMPC-KSA3", 128, new CipherKeyGenerator());
    //     }
    // }
    // END android-removed

    // HMAC Related secret keys..
  
    // BEGIN android-removed
    // /**
    //  * MD2HMAC
    //  */
    // public static class MD2HMAC
    //     extends JCEKeyGenerator
    // {
    //     public MD2HMAC()
    //     {
    //         super("HMACMD2", 128, new CipherKeyGenerator());
    //     }
    // }
    //
    //
    // /**
    //  * MD4HMAC
    //  */
    // public static class MD4HMAC
    //     extends JCEKeyGenerator
    // {
    //     public MD4HMAC()
    //     {
    //         super("HMACMD4", 128, new CipherKeyGenerator());
    //     }
    // }
    // END android-removed

    /**
     * MD5HMAC
     */
    public static class MD5HMAC
        extends JCEKeyGenerator
    {
        public MD5HMAC()
        {
            super("HMACMD5", 128, new CipherKeyGenerator());
        }
    }


    // /**
    //  * RIPE128HMAC
    //  */
    // public static class RIPEMD128HMAC
    //     extends JCEKeyGenerator
    // {
    //     public RIPEMD128HMAC()
    //     {
    //         super("HMACRIPEMD128", 128, new CipherKeyGenerator());
    //     }
    // }

    // /**
    //  * RIPE160HMAC
    //  */
    // public static class RIPEMD160HMAC
    //     extends JCEKeyGenerator
    // {
    //     public RIPEMD160HMAC()
    //     {
    //         super("HMACRIPEMD160", 160, new CipherKeyGenerator());
    //     }
    // }


    /**
     * HMACSHA1
     */
    public static class HMACSHA1
        extends JCEKeyGenerator
    {
        public HMACSHA1()
        {
            super("HMACSHA1", 160, new CipherKeyGenerator());
        }
    }

    // BEGIN android-removed
    // /**
    //  * HMACSHA224
    //  */
    // public static class HMACSHA224
    //     extends JCEKeyGenerator
    // {
    //     public HMACSHA224()
    //     {
    //         super("HMACSHA224", 224, new CipherKeyGenerator());
    //     }
    // }
    // END android-removed
    
    /**
     * HMACSHA256
     */
    public static class HMACSHA256
        extends JCEKeyGenerator
    {
        public HMACSHA256()
        {
            super("HMACSHA256", 256, new CipherKeyGenerator());
        }
    }
    
    /**
     * HMACSHA384
     */
    public static class HMACSHA384
        extends JCEKeyGenerator
    {
        public HMACSHA384()
        {
            super("HMACSHA384", 384, new CipherKeyGenerator());
        }
    }
    
    /**
     * HMACSHA512
     */
    public static class HMACSHA512
        extends JCEKeyGenerator
    {
        public HMACSHA512()
        {
            super("HMACSHA512", 512, new CipherKeyGenerator());
        }
    }
    
    // BEGIN android-removed
    // /**
    //  * HMACTIGER
    //  */
    // public static class HMACTIGER
    //     extends JCEKeyGenerator
    // {
    //     public HMACTIGER()
    //     {
    //         super("HMACTIGER", 192, new CipherKeyGenerator());
    //     }
    // }
    // END android-removed
}
