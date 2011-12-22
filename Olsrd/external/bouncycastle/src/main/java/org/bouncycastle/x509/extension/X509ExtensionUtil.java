package org.bouncycastle.x509.extension;

import org.bouncycastle.asn1.ASN1Object;
import org.bouncycastle.asn1.ASN1OctetString;
import org.bouncycastle.asn1.DERObjectIdentifier;
import org.bouncycastle.asn1.DEROctetString;
import org.bouncycastle.asn1.DERSequence;
import org.bouncycastle.asn1.DERString;
import org.bouncycastle.asn1.x509.GeneralName;
import org.bouncycastle.asn1.x509.X509Extensions;
import org.bouncycastle.asn1.x509.X509Name;

import java.io.IOException;
import java.security.cert.CertificateParsingException;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.Enumeration;
import java.util.List;


public class X509ExtensionUtil
{
    public static ASN1Object fromExtensionValue(
        byte[]  encodedValue) 
        throws IOException
    {
        ASN1OctetString octs = (ASN1OctetString)ASN1Object.fromByteArray(encodedValue);
        
        return ASN1Object.fromByteArray(octs.getOctets());
    }

    public static Collection getIssuerAlternativeNames(X509Certificate cert)
            throws CertificateParsingException
    {
        byte[] extVal = cert.getExtensionValue(X509Extensions.IssuerAlternativeName.getId());

        return getAlternativeNames(extVal);
    }

    public static Collection getSubjectAlternativeNames(X509Certificate cert)
            throws CertificateParsingException
    {        
        byte[] extVal = cert.getExtensionValue(X509Extensions.SubjectAlternativeName.getId());

        return getAlternativeNames(extVal);
    }

    private static Collection getAlternativeNames(byte[] extVal)
        throws CertificateParsingException
    {
        if (extVal == null)
        {
            return Collections.EMPTY_LIST;
        }
        try
        {
            Collection temp = new ArrayList();
            Enumeration it = DERSequence.getInstance(fromExtensionValue(extVal)).getObjects();
            while (it.hasMoreElements())
            {
                GeneralName genName = GeneralName.getInstance(it.nextElement());
                List list = new ArrayList();
                // BEGIN android-changed
                list.add(Integer.valueOf(genName.getTagNo()));
                // END android-changed
                switch (genName.getTagNo())
                {
                case GeneralName.ediPartyName:
                case GeneralName.x400Address:
                case GeneralName.otherName:
                    list.add(genName.getName().getDERObject());
                    break;
                case GeneralName.directoryName:
                    list.add(X509Name.getInstance(genName.getName()).toString());
                    break;
                case GeneralName.dNSName:
                case GeneralName.rfc822Name:
                case GeneralName.uniformResourceIdentifier:
                    list.add(((DERString)genName.getName()).getString());
                    break;
                case GeneralName.registeredID:
                    list.add(DERObjectIdentifier.getInstance(genName.getName()).getId());
                    break;
                case GeneralName.iPAddress:
                    list.add(DEROctetString.getInstance(genName.getName()).getOctets());
                    break;
                default:
                    throw new IOException("Bad tag number: " + genName.getTagNo());
                }

                temp.add(list);
            }
            return Collections.unmodifiableCollection(temp);
        }
        catch (Exception e)
        {
            throw new CertificateParsingException(e.getMessage());
        }
    }
}
