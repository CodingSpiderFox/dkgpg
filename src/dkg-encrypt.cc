/*******************************************************************************
   This file is part of Distributed Privacy Guard (DKGPG).

 Copyright (C) 2017, 2018  Heiko Stamer <HeikoStamer@gmx.net>

   DKGPG is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   DKGPG is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with DKGPG; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
*******************************************************************************/

// include headers
#ifdef HAVE_CONFIG_H
	#include "dkgpg_config.h"
#endif

#include <vector>
#include <string>

#include <libTMCG.hh>
#include "dkg-io.hh"

bool encrypt_session_key
	(const TMCG_OpenPGP_Subkey* sub, const tmcg_openpgp_secure_octets_t &seskey,
	 const tmcg_openpgp_octets_t &subkeyid, tmcg_openpgp_octets_t &pkesk)
{
	gcry_error_t ret;
	if ((sub->pkalgo == TMCG_OPENPGP_PKALGO_RSA) ||
	    (sub->pkalgo == TMCG_OPENPGP_PKALGO_RSA_ENCRYPT_ONLY))
	{
		gcry_mpi_t me;
		me = gcry_mpi_new(2048);
		ret = CallasDonnerhackeFinneyShawThayerRFC4880::
			AsymmetricEncryptRSA(seskey, sub->key, me);
		if (ret)
		{
			std::cerr << "ERROR: AsymmetricEncryptRSA() failed (rc = " <<
				gcry_err_code(ret) << ")" << std::endl;
			return false;
		}
		CallasDonnerhackeFinneyShawThayerRFC4880::
			PacketPkeskEncode(subkeyid, me, pkesk);
		gcry_mpi_release(me);
	}
	else if (sub->pkalgo == TMCG_OPENPGP_PKALGO_ELGAMAL)
	{	
		// Note that OpenPGP ElGamal encryption in $Z^*_p$ provides only
		// OW-CPA security under the CDH assumption. In order to achieve
		// at least IND-CPA (aka semantic) security under DDH assumption
		// the encoded message $m$ must be an element of the prime-order
		// subgroup $G_q$ generated by $g$ (algebraic structure of tElG).
		// Unfortunately, the probability that this happens is negligible,
		// if the size of prime $q$ is much smaller than the size of $p$.
		// We cannot enforce $m\in G_q$ since $m$ is padded according to
		// OpenPGP (PKCS#1). Thus, one bit of the session key is leaked.
		gcry_mpi_t gk, myk;
		gk = gcry_mpi_new(2048);
		myk = gcry_mpi_new(2048);
		ret = CallasDonnerhackeFinneyShawThayerRFC4880::
			AsymmetricEncryptElgamal(seskey, sub->key, gk, myk);
		if (ret)
		{
			std::cerr << "ERROR: AsymmetricEncryptElgamal() failed" <<
				" (rc = " << gcry_err_code(ret) << ")" << std::endl;
			return false;
		}
		CallasDonnerhackeFinneyShawThayerRFC4880::
			PacketPkeskEncode(subkeyid, gk, myk, pkesk);
		gcry_mpi_release(gk);
		gcry_mpi_release(myk);
	}
	else if (sub->pkalgo == TMCG_OPENPGP_PKALGO_ECDH)
	{
		tmcg_openpgp_octets_t rcpfpr;
		gcry_mpi_t ecepk;
		size_t rkwlen = 0;
		tmcg_openpgp_byte_t rkw[256];
		CallasDonnerhackeFinneyShawThayerRFC4880::
			FingerprintCompute(sub->sub_hashing, rcpfpr);
		ecepk = gcry_mpi_new(1024);
		memset(rkw, 0, sizeof(rkw));
		ret = CallasDonnerhackeFinneyShawThayerRFC4880::
			AsymmetricEncryptECDH(seskey, sub->key, sub->kdf_hashalgo,
				sub->kdf_skalgo, sub->ec_curve, rcpfpr, ecepk,
				rkwlen, rkw);
		if (ret)
		{
			std::cerr << "ERROR: AsymmetricEncryptECDH() failed" <<
				" (rc = " << gcry_err_code(ret) << ")" << std::endl;
			gcry_mpi_release(ecepk);
			return false;
		}
		CallasDonnerhackeFinneyShawThayerRFC4880::
			PacketPkeskEncode(subkeyid, ecepk, rkwlen, rkw, pkesk);
		gcry_mpi_release(ecepk);
	}
	else
	{
		std::cerr << "ERROR: public-key algorithm " << (int)sub->pkalgo <<
			" not supported" << std::endl;
		return false;
	}
	return true;
}

bool encrypt_session_key
	(const TMCG_OpenPGP_Pubkey* pub, const tmcg_openpgp_secure_octets_t &seskey,
	 const tmcg_openpgp_octets_t &keyid, tmcg_openpgp_octets_t &pkesk)
{
	bool ret;
	if (pub->pkalgo != TMCG_OPENPGP_PKALGO_RSA)
	{
		return false; // only RSA is an encryption-capable primary key 
	}
	TMCG_OpenPGP_Subkey *sub = new TMCG_OpenPGP_Subkey(pub->pkalgo,
		pub->creationtime, pub->expirationtime, pub->rsa_n, pub->rsa_e,
		pub->packet);
	ret = encrypt_session_key(sub, seskey, keyid, pkesk);
	delete sub;
	return ret;
}

int main
	(int argc, char **argv)
{
	static const char *usage = "dkg-encrypt [OPTIONS] KEYSPEC";
	static const char *about = PACKAGE_STRING " " PACKAGE_URL;
	static const char *version = PACKAGE_VERSION " (" PACKAGE_NAME ")";

	std::vector<std::string> keyspec;
	std::string	ifilename, ofilename, s, kfilename;
	int		opt_verbose = 0;
	bool	opt_binary = false, opt_weak = false, opt_t = false, opt_r = false;
	char	*opt_i = NULL;
	char	*opt_o = NULL;
	char	*opt_s = NULL;
	char	*opt_k = NULL;
	unsigned long int opt_a = 0;

	// parse argument list
	for (size_t i = 0; i < (size_t)(argc - 1); i++)
	{
		std::string arg = argv[i+1];
		// ignore options
		if ((arg.find("-i") == 0) || (arg.find("-o") == 0) ||
			(arg.find("-s") == 0) || (arg.find("-k") == 0) ||
			(arg.find("-a") == 0))
		{
			size_t idx = ++i;
			if ((arg.find("-i") == 0) && (idx < (size_t)(argc - 1)) &&
				(opt_i == NULL))
			{
				ifilename = argv[i+1];
				opt_i = (char*)ifilename.c_str();
			}
			if ((arg.find("-o") == 0) && (idx < (size_t)(argc - 1)) &&
				(opt_o == NULL))
			{
				ofilename = argv[i+1];
				opt_o = (char*)ofilename.c_str();
			}
			if ((arg.find("-s") == 0) && (idx < (size_t)(argc - 1)) &&
				(opt_s == NULL))
			{
				s = argv[i+1];
				opt_s = (char*)s.c_str();
			}
			if ((arg.find("-k") == 0) && (idx < (size_t)(argc - 1)) &&
				(opt_k == NULL))
			{
				kfilename = argv[i+1];
				opt_k = (char*)kfilename.c_str();
			}
			if ((arg.find("-a") == 0) && (idx < (size_t)(argc - 1)) &&
				(opt_a == 0))
			{
				opt_a = strtoul(argv[i+1], NULL, 10);
			}
			continue;
		}
		else if ((arg.find("--") == 0) || (arg.find("-b") == 0) ||
			(arg.find("-v") == 0) || (arg.find("-h") == 0) ||
			(arg.find("-V") == 0) || (arg.find("-w") == 0) ||
			(arg.find("-t") == 0) || (arg.find("-r") == 0))
		{
			if ((arg.find("-h") == 0) || (arg.find("--help") == 0))
			{
				std::cout << usage << std::endl;
				std::cout << about << std::endl;
				std::cout << "Arguments mandatory for long options are also" <<
					" mandatory for short options." << std::endl;
				std::cout << "  -a INTEGER          enforce use of AEAD" <<
					" algorithm INTEGER (cf. RFC 4880bis)" << std::endl;
				std::cout << "  -b, --binary        write encrypted message" <<
					" in binary format (only if -i)" << std::endl;
				std::cout << "  -h, --help          print this help" <<
					std::endl;
				std::cout << "  -i FILENAME         read message rather" <<
					" from FILENAME than STDIN" << std::endl;
				std::cout << "  -k FILENAME         use keyring FILENAME" <<
					" containing the required keys" << std::endl;
				std::cout << "  -o FILENAME         write encrypted message" <<
					" rather to FILENAME than STDOUT" << std::endl;
				std::cout << "  -r, --recipients    select key(s) from given" <<
					" keyring by KEYSPEC" << std::endl; 
				std::cout << "  -s STRING           select only encryption" <<
					"-capable subkeys with fingerprint equals STRING" <<
					std::endl;
				std::cout << "  -t, --throw-keyids  throw included key IDs" <<
					" for somewhat improved privacy" << std::endl;
				std::cout << "  -v, --version       print the version" <<
					" number" << std::endl;
				std::cout << "  -V, --verbose       turn on verbose output" <<
					std::endl;
				std::cout << "  -w, --weak          allow weak keys" <<
					std::endl;
				return 0; // not continue
			}
			if ((arg.find("-b") == 0) || (arg.find("--binary") == 0))
				opt_binary = true;
			if ((arg.find("-r") == 0) || (arg.find("--recipients") == 0))
				opt_r = true;
			if ((arg.find("-t") == 0) || (arg.find("--throw-keyids") == 0))
				opt_t = true;
			if ((arg.find("-v") == 0) || (arg.find("--version") == 0))
			{
				std::cout << "dkg-encrypt v" << version << std::endl;
				return 0; // not continue
			}
			if ((arg.find("-V") == 0) || (arg.find("--verbose") == 0))
				opt_verbose++; // increase verbosity
			if ((arg.find("-w") == 0) || (arg.find("--weak") == 0))
				opt_weak = true;
			continue;
		}
		else if (arg.find("-") == 0)
		{
			std::cerr << "ERROR: unknown option \"" << arg << "\"" << std::endl;
			return -1;
		}
		keyspec.push_back(arg);
	}

	// lock memory
	bool force_secmem = false;
	if (!lock_memory())
	{
		std::cerr << "WARNING: locking memory failed; CAP_IPC_LOCK required" <<
			" for full memory protection" << std::endl;
		// at least try to use libgcrypt's secure memory
		force_secmem = true;
	}

	// initialize LibTMCG
	if (!init_libTMCG(force_secmem))
	{
		std::cerr << "ERROR: initialization of LibTMCG failed" << std::endl;
		return -1;
	}
	if (opt_verbose)
		std::cerr << "INFO: using LibTMCG version " <<
			version_libTMCG() << std::endl;

#ifdef DKGPG_TESTSUITE
	keyspec.push_back("Test1_dkg-pub.asc");
	if (!opt_binary)
		opt_binary = true;
	ofilename = "Test1_output.bin";
	opt_o = (char*)ofilename.c_str();
	opt_verbose = 2;
	if (tmcg_mpz_wrandom_ui() % 2)
		opt_t = true;
#if GCRYPT_VERSION_NUMBER < 0x010700
	// FIXME: remove, if libgcrypt >= 1.7.0 required by configure.ac
#else
	if (tmcg_mpz_wrandom_ui() % 2)
		opt_a = 2; // sometimes test AEAD with OCB
#endif
#else
#ifdef DKGPG_TESTSUITE_Y
	keyspec.push_back("TestY-pub.asc");
	ofilename = "TestY_output.asc";
	opt_o = (char*)ofilename.c_str();
	opt_verbose = 2;
	if (tmcg_mpz_wrandom_ui() % 2)
		opt_t = true;
#if GCRYPT_VERSION_NUMBER < 0x010700
	// FIXME: remove, if libgcrypt >= 1.7.0 required by configure.ac
#else
	if (tmcg_mpz_wrandom_ui() % 2)
		opt_a = 2; // sometimes test AEAD with OCB
#endif
#endif
#endif

	// check command line arguments
	if (keyspec.size() < 1)
	{
		std::cerr << "ERROR: argument KEYSPEC is missing; usage: " <<
			usage << std::endl;
		return -1;
	}
	if (!opt_r && (keyspec.size() > 1))
	{
		std::cerr << "ERROR: wrong KEYSPEC; more than one file" <<
			"is not supported" << std::endl;
		return -1;
	}

	// read the public key from file
	std::string armored_pubkey;
	if (!opt_r)
	{
		if (!read_key_file(keyspec[0], armored_pubkey))
			return -1;
	}

	// read the keyring
	std::string armored_pubring;
	if ((opt_k != NULL) && !read_key_file(kfilename, armored_pubring))
		return -1;

	// parse the keyring
	TMCG_OpenPGP_Keyring *ring = NULL;
	bool parse_ok;
	if (opt_k != NULL)
	{
		parse_ok = CallasDonnerhackeFinneyShawThayerRFC4880::
			PublicKeyringParse(armored_pubring, opt_verbose, ring);
		if (!parse_ok)
		{
			std::cerr << "WARNING: cannot use the given keyring" << std::endl;
			ring = new TMCG_OpenPGP_Keyring(); // create an empty keyring
		}
	}
	else
		ring = new TMCG_OpenPGP_Keyring(); // create an empty keyring

	// read message from stdin or file
	tmcg_openpgp_octets_t msg;
#ifdef DKGPG_TESTSUITE
	std::string test_msg = "This is just a simple test message.";
	for (size_t i = 0; i < test_msg.length(); i++)
		msg.push_back(test_msg[i]);
#else
#ifdef DKGPG_TESTSUITE_Y
	std::string test_msg = "This is just another simple test message.";
	for (size_t i = 0; i < test_msg.length(); i++)
		msg.push_back(test_msg[i]);
#else
	if (opt_i != NULL)
	{
		std::string input_msg;
		if (!read_message(ifilename, input_msg))
		{
			delete ring;
			return -1;
		}
		for (size_t i = 0; i < input_msg.length(); i++)
			msg.push_back(input_msg[i]);
	}
	else
	{
		char c;
		while (std::cin.get(c))
			msg.push_back(c);
		std::cin.clear();
	}
#endif
#endif

	// encrypt the provided message and create MDC
	gcry_error_t ret;
	tmcg_openpgp_octets_t lit, prefix, enc;
	tmcg_openpgp_secure_octets_t seskey;
	CallasDonnerhackeFinneyShawThayerRFC4880::PacketLitEncode(msg, lit);
	ret = CallasDonnerhackeFinneyShawThayerRFC4880::SymmetricEncryptAES256(lit,
		seskey, prefix, true, enc); // seskey and prefix only
	if (ret)
	{
		std::cerr << "ERROR: SymmetricEncryptAES256() failed (rc = " <<
			gcry_err_code(ret) << ")" << std::endl;
		delete ring;
		return ret;
	}
	tmcg_openpgp_octets_t mdc_hashing, hash, mdc, seipd;
	enc.clear();
	// "it includes the prefix data described above" [RFC 4880]
	mdc_hashing.insert(mdc_hashing.end(), prefix.begin(), prefix.end());
	// "it includes all of the plaintext" [RFC 4880]
	mdc_hashing.insert(mdc_hashing.end(), lit.begin(), lit.end());
	// "and the also includes two octets of values 0xD3, 0x14" [RFC 4880]
	mdc_hashing.push_back(0xD3);
	mdc_hashing.push_back(0x14);
	hash.clear();
	// "passed through the SHA-1 hash function" [RFC 4880]
	CallasDonnerhackeFinneyShawThayerRFC4880::
		HashCompute(TMCG_OPENPGP_HASHALGO_SHA1, mdc_hashing, hash);
	CallasDonnerhackeFinneyShawThayerRFC4880::PacketMdcEncode(hash, mdc);
	lit.insert(lit.end(), mdc.begin(), mdc.end()); // append MDC packet
	// generate a fresh session key, but keep the previous prefix
	seskey.clear();
	ret = CallasDonnerhackeFinneyShawThayerRFC4880::SymmetricEncryptAES256(lit,
		seskey, prefix, false, enc); // encryption of literal packet + MDC
	if (ret)
	{
		std::cerr << "ERROR: SymmetricEncryptAES256() failed (rc = " <<
			gcry_err_code(ret) << ")" << std::endl;
		delete ring;
		return ret;
	}
	CallasDonnerhackeFinneyShawThayerRFC4880::PacketSeipdEncode(enc, seipd);

	tmcg_openpgp_octets_t aead;
	tmcg_openpgp_aeadalgo_t aeadalgo = TMCG_OPENPGP_AEADALGO_OCB; // default
#if GCRYPT_VERSION_NUMBER < 0x010900
	// FIXME: remove, if libgcrypt >= 1.9.0 required by configure.ac
#else
	aeadalgo = TMCG_OPENPGP_AEADALGO_EAX;
#endif
#if GCRYPT_VERSION_NUMBER < 0x010700
	// FIXME: remove, if libgcrypt >= 1.7.0 required by configure.ac
#else
	// additionally, encrypt the message with appropriate AEAD algorithm
	if (opt_a != 0)
		aeadalgo = (tmcg_openpgp_aeadalgo_t)opt_a; // enforce given algorithm
	lit.clear(), enc.clear();
	CallasDonnerhackeFinneyShawThayerRFC4880::PacketLitEncode(msg, lit);
	tmcg_openpgp_octets_t ad, iv;
	ad.push_back(0xD4); // packet tag in new format
	ad.push_back(0x01); // packet version number
	ad.push_back(TMCG_OPENPGP_SKALGO_AES256); // cipher algorithm octet
	ad.push_back(aeadalgo); // AEAD algorithm octet
	ad.push_back(10); // chunk size octet (chunk of size 2^16 bytes)
	for (size_t i = 0; i < 8; i++)
		ad.push_back(0x00); // initial eight-octet big-endian chunk index
	ret = CallasDonnerhackeFinneyShawThayerRFC4880::SymmetricEncryptAEAD(lit,
		seskey, TMCG_OPENPGP_SKALGO_AES256, aeadalgo, 10, ad, opt_verbose,
		iv, enc); 
	if (ret)
	{
		std::cerr << "ERROR: SymmetricEncryptAEAD() failed (rc = " <<
			gcry_err_code(ret) << ")" << std::endl;
		delete ring;
		return ret;
	}
	CallasDonnerhackeFinneyShawThayerRFC4880::PacketAeadEncode(
		TMCG_OPENPGP_SKALGO_AES256, aeadalgo, 10, iv, enc, aead);
#endif

	// iterate through all specified encryption keys
	tmcg_openpgp_octets_t all;
	size_t features = 0xFF;
	for (size_t k = 0; k < keyspec.size(); k++)
	{
		TMCG_OpenPGP_Pubkey *primary = NULL;
		if (opt_r)
		{
			// try to extract the public key from keyring by keyspec
			if (opt_verbose > 1)
				std::cerr << "INFO: lookup for encryption key with" <<
					" fingerprint " << keyspec[k] << std::endl;
			const TMCG_OpenPGP_Pubkey *key = ring->FindByKeyid(keyspec[k]);
			if (key == NULL)
			{
				std::cerr << "ERROR: encryption key not found in keyring" <<
					std::endl; 
				delete ring;
				return -1;
			}
			tmcg_openpgp_octets_t pkts;
			key->Export(pkts);
			armored_pubkey = "";
			CallasDonnerhackeFinneyShawThayerRFC4880::
				ArmorEncode(TMCG_OPENPGP_ARMOR_PUBLIC_KEY_BLOCK, pkts,
					armored_pubkey);
		}

		// parse the public key block and check corresponding signatures
		parse_ok = CallasDonnerhackeFinneyShawThayerRFC4880::
			PublicKeyBlockParse(armored_pubkey, opt_verbose, primary);
		if (parse_ok)
		{
			primary->CheckSelfSignatures(ring, opt_verbose);
			if (!primary->valid)
			{
				if (opt_weak)
				{
					std::cerr << "WARNING: primary key is not valid" <<
						std::endl;
				}
				else
				{
					std::cerr << "ERROR: primary key is not valid" << std::endl;
					delete primary;
					delete ring;
					return -1;
				}
			}
			primary->CheckSubkeys(ring, opt_verbose);
			primary->Reduce(); // keep only valid subkeys
			if (primary->Weak(opt_verbose) && !opt_weak)
			{
				std::cerr << "ERROR: weak primary key is not allowed" <<
					std::endl;
				delete primary;
				delete ring;
				return -1;
			}
		}
		else
		{
			std::cerr << "ERROR: cannot use the provided public key" <<
				std::endl;
			delete ring;
			return -1;
		}

		// select encryption-capable subkeys
		std::vector<TMCG_OpenPGP_Subkey*> selected;
		for (size_t j = 0; j < primary->subkeys.size(); j++)
		{
			// subkey not selected?
			std::string kid, fpr;
			CallasDonnerhackeFinneyShawThayerRFC4880::
				KeyidCompute(primary->subkeys[j]->sub_hashing, kid);
			CallasDonnerhackeFinneyShawThayerRFC4880::
				FingerprintCompute(primary->subkeys[j]->sub_hashing, fpr);
			if (opt_s && (kid != s) && (fpr != s))
				continue;
			// encryption-capable subkey?
			if (((primary->subkeys[j]->AccumulateFlags() & 0x04) == 0x04) ||
			    ((primary->subkeys[j]->AccumulateFlags() & 0x08) == 0x08) ||
			    (!primary->subkeys[j]->AccumulateFlags() &&
					((primary->subkeys[j]->pkalgo == TMCG_OPENPGP_PKALGO_RSA) || 
					(primary->subkeys[j]->pkalgo ==
						TMCG_OPENPGP_PKALGO_RSA_ENCRYPT_ONLY) ||
					(primary->subkeys[j]->pkalgo == TMCG_OPENPGP_PKALGO_ELGAMAL) ||
					(primary->subkeys[j]->pkalgo == TMCG_OPENPGP_PKALGO_ECDH))))
			{
				if (primary->subkeys[j]->Weak(opt_verbose) && !opt_weak)
				{
					if (opt_verbose)
						std::cerr << "WARNING: weak subkey for encryption" <<
							" ignored" << std::endl;
				}
				else if ((primary->subkeys[j]->pkalgo != 
							TMCG_OPENPGP_PKALGO_RSA) &&
				         (primary->subkeys[j]->pkalgo !=
							TMCG_OPENPGP_PKALGO_RSA_ENCRYPT_ONLY) &&
				         (primary->subkeys[j]->pkalgo !=
							TMCG_OPENPGP_PKALGO_ELGAMAL) &&
					 (primary->subkeys[j]->pkalgo !=
							TMCG_OPENPGP_PKALGO_ECDH))
				{
					if (opt_verbose)
						std::cerr << "WARNING: subkey with unsupported" <<
							" public-key algorithm for encryption ignored" <<
							std::endl;
				}
				else
				{
					selected.push_back(primary->subkeys[j]);
					size_t skf = primary->subkeys[j]->AccumulateFeatures(); 
					features &= (skf | primary->AccumulateFeatures());
					if ((std::find(primary->subkeys[j]->psa.begin(),
						primary->subkeys[j]->psa.end(), TMCG_OPENPGP_SKALGO_AES256)
							== primary->subkeys[j]->psa.end()) &&
					    (std::find(primary->psa.begin(), primary->psa.end(),
							TMCG_OPENPGP_SKALGO_AES256) == primary->psa.end()))
					{
						if (opt_verbose)
							std::cerr << "WARNING: AES-256 is none of the" <<
								" preferred symmetric algorithms;" <<
								" use AES-256 anyway" << std::endl;
					}
					if (((skf & 0x01) != 0x01) && !opt_a &&
					    ((primary->AccumulateFeatures() & 0x01) != 0x01))
					{
						if (opt_verbose)
							std::cerr << "WARNING: recipient does not state" <<
								" support for modification detection (MDC);" <<
								"use MDC anyway" << std::endl;
					}
					if (((skf & 0x02) != 0x02) && !opt_a &&
					    ((primary->AccumulateFeatures() & 0x02) != 0x02))
					{
						if (opt_verbose)
							std::cerr << "WARNING: recipient does not state" <<
								" support for AEAD Encrypted Data Packet;" <<
								" AEAD disabled" << std::endl;
					}
					if (((skf & 0x02) != 0x02) && opt_a &&
					    ((primary->AccumulateFeatures() & 0x02) != 0x02))
					{
						if (opt_verbose)
							std::cerr << "WARNING: recipient does not state" <<
								" support for AEAD Encrypted Data Packet;" <<
								" AEAD enforced by option -a" << std::endl;
					}
					if ((std::find(primary->subkeys[j]->paa.begin(),
						primary->subkeys[j]->paa.end(), aeadalgo)
							== primary->subkeys[j]->paa.end()) &&
					    (std::find(primary->paa.begin(), primary->paa.end(),
							aeadalgo) == primary->paa.end()))
					{
						if (opt_verbose)
							std::cerr << "WARNING: selected algorithm is none" <<
								" of the preferred AEAD algorithms" << std::endl;
					}
				}
			}
		}

		// check primary key, if no encryption-capable subkeys have been
		// selected previously
		if ((selected.size() == 0) && primary->valid) 
		{	
			if (((primary->AccumulateFlags() & 0x04) != 0x04) &&
			    ((primary->AccumulateFlags() & 0x08) != 0x08) &&
			    (!primary->AccumulateFlags() &&
					(primary->pkalgo != TMCG_OPENPGP_PKALGO_RSA) &&
					(primary->pkalgo != TMCG_OPENPGP_PKALGO_RSA_ENCRYPT_ONLY)))
			{
				std::cerr << "ERROR: no encryption-capable RSA public key" <<
					" found" << std::endl;
				delete primary;
				delete ring;
				return -1;
			}
			features &= primary->AccumulateFeatures();
			if (std::find(primary->psa.begin(), primary->psa.end(),
				TMCG_OPENPGP_SKALGO_AES256) == primary->psa.end())
			{
				if (opt_verbose)
					std::cerr << "WARNING: AES-256 is none of the preferred" <<
						" symmetric algorithms; use AES-256 anyway" << std::endl;
			}
			if (((primary->AccumulateFeatures() & 0x01) != 0x01) && !opt_a)
			{
				if (opt_verbose)
					std::cerr << "WARNING: recipient does not state support" <<
						" for modification detection (MDC);" <<
						"use MDC protection anyway" << std::endl;
			}
			if (((primary->AccumulateFeatures() & 0x02) != 0x02) && !opt_a)
			{
				if (opt_verbose)
					std::cerr << "WARNING: recipient does not state support" <<
						" for AEAD Encrypted Data Packet; AEAD disabled" <<
						std::endl;
			}
			if (((primary->AccumulateFeatures() & 0x02) != 0x02) && opt_a)
			{
				if (opt_verbose)
					std::cerr << "WARNING: recipient does not state support" <<
						" for AEAD Encrypted Data Packet; AEAD enforced by" <<
						" option -a" << std::endl;
			}
			if (std::find(primary->paa.begin(), primary->paa.end(), aeadalgo) ==
				primary->paa.end())
			{
				if (opt_verbose)
					std::cerr << "WARNING: selected algorithm is none of the" <<
						" preferred AEAD algorithms" << std::endl;
			}
		}
		else if ((selected.size() == 0) && !primary->valid)
		{
			std::cerr << "ERROR: no valid public key found for encryption" <<
				std::endl;
			delete primary;
			delete ring;
			return -1;
		}

		// encrypt the session key (create PKESK packet)
		if (opt_verbose > 1)
			std::cerr << "INFO: " << selected.size() << " subkeys selected" <<
				" for encryption of session key" << std::endl;
		for (size_t j = 0; j < selected.size(); j++)
		{
			tmcg_openpgp_octets_t pkesk, subkeyid;
			if (opt_t)
			{
				// An implementation MAY accept or use a Key ID of zero as a
				// "wild card" or "speculative" Key ID. In this case, the
				// receiving implementation would try all available private keys,
				// checking for a valid decrypted session key. This format helps
				// reduce traffic analysis of messages. [RFC4880]
				for (size_t i = 0; i < 8; i++)
					subkeyid.push_back(0x00);
			}
			else
			{
				subkeyid.insert(subkeyid.end(),
					selected[j]->id.begin(), selected[j]->id.end());
			}
			if (!encrypt_session_key(selected[j], seskey, subkeyid, pkesk))
			{
				delete primary;
				delete ring;
				return -1;
			}
			all.insert(all.end(), pkesk.begin(), pkesk.end());
		}
		if (selected.size() == 0)
		{
			tmcg_openpgp_octets_t pkesk, keyid;
			if (opt_t)
			{
				// An implementation MAY accept or use a Key ID of zero as a
				// "wild card" or "speculative" Key ID. In this case, the
				// receiving implementation would try all available private keys,
				// checking for a valid decrypted session key. This format helps
				// reduce traffic analysis of messages. [RFC4880]
				for (size_t i = 0; i < 8; i++)
					keyid.push_back(0x00);
			}
			else
			{
				keyid.insert(keyid.end(),
					primary->id.begin(), primary->id.end());
			}
			if (!encrypt_session_key(primary, seskey, keyid, pkesk))
			{
				delete primary;
				delete ring;
				return -1;
			}
			all.insert(all.end(), pkesk.begin(), pkesk.end());
		}

		// release primary key
		delete primary;
	}

	// append the encrypted data packet(s) according to supported features
	if (((features & 0x02) == 0x02) && (aead.size() > 0))
	{
		// append AEAD, because all selected recipients/keys have support
		all.insert(all.end(), aead.begin(), aead.end());
	}
	else if (opt_a && (aead.size() > 0))
	{
		// append AEAD, because it use has been enforced by option -a
		all.insert(all.end(), aead.begin(), aead.end());
	}
	else
	{
		// append SEIPD, because some selected recipients/keys have no support
		all.insert(all.end(), seipd.begin(), seipd.end());
	}

	// encode all packages in ASCII armor
	std::string armored_message;
	CallasDonnerhackeFinneyShawThayerRFC4880::
		ArmorEncode(TMCG_OPENPGP_ARMOR_MESSAGE, all, armored_message);

	// write out the result
	if (opt_o != NULL)
	{
		if (opt_binary)
		{
			if (!write_message(ofilename, all))
			{
				delete ring;
				return -1;
			}
		}
		else
		{
			if (!write_message(ofilename, armored_message))
			{
				delete ring;
				return -1;
			}
		}
	}
	else
		std::cout << armored_message << std::endl;

	// release keyring
	delete ring;
	
	return 0;
}

