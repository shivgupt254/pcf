
#include "GarbledCct3.h"

void GarbledCct3::init()
{
	m_gate_ix = 0;

	m_gen_inp_hash_ix = 0;

	m_gen_inp_ix = 0;
	m_evl_inp_ix = 0;
	m_gen_out_ix = 0;
	m_evl_out_ix = 0;

	m_o_bufr.clear();
	m_i_bufr.clear();

	m_gen_inp_hash.resize(Env::key_size_in_bytes());

	Bytes tmp(16);
	for (size_t ix = 0; ix < Env::k(); ix++) { tmp.set_ith_bit(ix, 1); }
	m_clear_mask = _mm_loadu_si128(reinterpret_cast<__m128i*>(&tmp[0]));
}

void GarbledCct3::gen_init(const vector<Bytes> &ot_keys, const Bytes &gen_inp_mask, const Bytes &seed)
{
	m_ot_keys = &ot_keys;
	m_gen_inp_mask = gen_inp_mask;
	m_prng.srand(seed);

	// R is a random k-bit string whose 0-th bit has to be 1
	static Bytes tmp;

	tmp = m_prng.rand(Env::k());
	tmp.set_ith_bit(0, 1);
	tmp.resize(16, 0);
	m_R = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&tmp[0]));

	init();

	if (m_w == 0)
	{
		m_w = new __m128i[Env::circuit().m_cnt];
	}

	m_gen_inp_decom.resize(Env::circuit().gen_inp_cnt()*2);
}


const int CIRCUIT_HASH_BUFFER_SIZE = 10*1024*1024;


void GarbledCct3::com_init(const vector<Bytes> &ot_keys, const Bytes &gen_inp_mask, const Bytes &seed)
{
	gen_init(ot_keys, gen_inp_mask, seed);
	m_bufr.reserve(CIRCUIT_HASH_BUFFER_SIZE);
	m_bufr.clear();
	m_hash.init();
}


void GarbledCct3::evl_init(const vector<Bytes> &ot_keys, const Bytes &masked_gen_inp, const Bytes &evl_inp)
{
	init();

	m_ot_keys = &ot_keys;
	m_gen_inp_mask = masked_gen_inp;
	m_evl_inp = evl_inp;

	m_evl_out.resize((Env::circuit().evl_out_cnt()+7)/8);
	m_gen_out.resize((Env::circuit().gen_out_cnt()+7)/8);

	if (m_w == 0)
	{
		m_w = new __m128i[Env::circuit().m_cnt];
	}

	m_bufr.reserve(CIRCUIT_HASH_BUFFER_SIZE);
	m_bufr.clear();
	m_hash.init();

	m_gen_inp_com.resize(Env::circuit().gen_inp_cnt());
	m_gen_inp_decom.resize(Env::circuit().gen_inp_cnt());
}


void GarbledCct3::gen_next_gate(const Gate &current_gate)
{
	__m128i current_zero_key;

	if (current_gate.m_tag == Circuit::GEN_INP)
	{
		__m128i a[2];

		static Bytes tmp;

		tmp = m_prng.rand(Env::k());
		tmp.resize(16, 0);
		current_zero_key = _mm_loadu_si128(reinterpret_cast<__m128i*>(&tmp[0]));

		a[0] = _mm_load_si128(&current_zero_key);
		a[1] = _mm_xor_si128(current_zero_key, m_R);

		uint8_t bit = m_gen_inp_mask.get_ith_bit(m_gen_inp_ix);

		_mm_storeu_si128(reinterpret_cast<__m128i*>(&tmp[0]), a[bit]);
		m_gen_inp_decom[2*m_gen_inp_ix+0] =
			Bytes(tmp.begin(), tmp.begin()+Env::key_size_in_bytes())+m_prng.rand(Env::k());

		_mm_storeu_si128(reinterpret_cast<__m128i*>(&tmp[0]), a[1-bit]);
		m_gen_inp_decom[2*m_gen_inp_ix+1] =
			Bytes(tmp.begin(), tmp.begin()+Env::key_size_in_bytes())+m_prng.rand(Env::k());

		m_o_bufr += m_gen_inp_decom[2*m_gen_inp_ix+0].hash(Env::k());
		m_o_bufr += m_gen_inp_decom[2*m_gen_inp_ix+1].hash(Env::k());

		m_gen_inp_ix++;
	}
	else if (current_gate.m_tag == Circuit::EVL_INP)
	{
		__m128i a[2];

		// zero_key = m_prng.rand(Env::k());
		static Bytes tmp;

		tmp = m_prng.rand(Env::k());
		tmp.resize(16, 0);
		current_zero_key = _mm_loadu_si128(reinterpret_cast<__m128i*>(&tmp[0]));

		// a[0] = (*m_ot_keys)[2*m_evl_inp_ix+0];
		tmp = (*m_ot_keys)[2*m_evl_inp_ix+0];
		tmp.resize(16, 0);
		a[0] = _mm_loadu_si128(reinterpret_cast<__m128i*>(&tmp[0]));

		// a[1] = (*m_ot_keys)[2*m_evl_inp_ix+1];
		tmp = (*m_ot_keys)[2*m_evl_inp_ix+1];
		tmp.resize(16, 0);
		a[1] = _mm_loadu_si128(reinterpret_cast<__m128i*>(&tmp[0]));

		// a[0] ^= zero_key; a[1] ^= zero_key ^ R;
		a[0] = _mm_xor_si128(a[0], current_zero_key);
		a[1] = _mm_xor_si128(a[1], _mm_xor_si128(current_zero_key, m_R));

		// m_o_bufr += a[0];
		_mm_storeu_si128(reinterpret_cast<__m128i*>(&tmp[0]), a[0]);
		m_o_bufr.insert(m_o_bufr.end(), tmp.begin(), tmp.begin()+Env::key_size_in_bytes());

		// m_o_bufr += a[1];
		_mm_storeu_si128(reinterpret_cast<__m128i*>(&tmp[0]), a[1]);
		m_o_bufr.insert(m_o_bufr.end(), tmp.begin(), tmp.begin()+Env::key_size_in_bytes());

		m_evl_inp_ix++;
	}
	else
	{
		const vector<uint64_t> &inputs = current_gate.m_input_idx;
		assert(inputs.size() == 1 || inputs.size() == 2);

#ifdef FREE_XOR
		if (is_xor(current_gate))
		{
			current_zero_key = inputs.size() == 2?
				_mm_xor_si128(m_w[inputs[0]], m_w[inputs[1]]) : _mm_load_si128(m_w+inputs[0]);
		}
		else
#endif
		if (inputs.size() == 2) // 2-arity gates
		{
			uint8_t bit;
			__m128i aes_key[2], aes_plaintext, aes_ciphertext;
			__m128i X[2], Y[2], Z[2];
			static Bytes tmp(16, 0);

			aes_plaintext = _mm_set1_epi64x(m_gate_ix);

			X[0] = _mm_load_si128(m_w+inputs[0]);
			Y[0] = _mm_load_si128(m_w+inputs[1]);

			X[1] = _mm_xor_si128(X[0], m_R); // X[1] = X[0] ^ R
			Y[1] = _mm_xor_si128(Y[0], m_R); // Y[1] = Y[0] ^ R

			const uint8_t perm_x = _mm_extract_epi8(X[0], 0) & 0x01; // permutation bit for X
			const uint8_t perm_y = _mm_extract_epi8(Y[0], 0) & 0x01; // permutation bit for Y
			const uint8_t de_garbled_ix = (perm_y<<1)|perm_x;

			// encrypt the 0-th entry : (X[x], Y[y])
			aes_key[0] = _mm_load_si128(X+perm_x);
			aes_key[1] = _mm_load_si128(Y+perm_y);

			KDF256((uint8_t*)&aes_plaintext, (uint8_t*)&aes_ciphertext, (uint8_t*)aes_key);
			aes_ciphertext = _mm_and_si128(aes_ciphertext, m_clear_mask); // clear extra bits so that only k bits left
			bit = current_gate.m_table[de_garbled_ix];

#ifdef GRR
			// GRR technique: using zero entry's key as one of the output keys
			_mm_store_si128(Z+bit, aes_ciphertext);
			Z[1-bit] = _mm_xor_si128(Z[bit], m_R);
			current_zero_key = _mm_load_si128(Z);
#else
			tmp = m_prng.rand(Env::k());
			tmp.resize(16, 0);
			Z[0] = _mm_loadu_si128(reinterpret_cast<__m128i*>(&tmp[0]));
			Z[1] = _mm_xor_si128(Z[0], m_R);

			aes_ciphertext = _mm_xor_si128(aes_ciphertext, Z[bit]);
			_mm_storeu_si128(reinterpret_cast<__m128i*>(&tmp[0]), aes_ciphertext);
			m_o_bufr.insert(m_o_bufr.end(), tmp.begin(), tmp.begin()+Env::key_size_in_bytes());
#endif

			// encrypt the 1st entry : (X[1-x], Y[y])
			aes_key[0] = _mm_xor_si128(aes_key[0], m_R);

			KDF256((uint8_t*)&aes_plaintext, (uint8_t*)&aes_ciphertext, (uint8_t*)aes_key);
			aes_ciphertext = _mm_and_si128(aes_ciphertext, m_clear_mask);
			bit = current_gate.m_table[0x01^de_garbled_ix];
			aes_ciphertext = _mm_xor_si128(aes_ciphertext, Z[bit]);
			_mm_storeu_si128(reinterpret_cast<__m128i*>(&tmp[0]), aes_ciphertext);
			m_o_bufr.insert(m_o_bufr.end(), tmp.begin(), tmp.begin()+Env::key_size_in_bytes());

			// encrypt the 2nd entry : (X[x], Y[1-y])
			aes_key[0] = _mm_xor_si128(aes_key[0], m_R);
			aes_key[1] = _mm_xor_si128(aes_key[1], m_R);

			KDF256((uint8_t*)&aes_plaintext, (uint8_t*)&aes_ciphertext, (uint8_t*)aes_key);
			aes_ciphertext = _mm_and_si128(aes_ciphertext, m_clear_mask);
			bit = current_gate.m_table[0x02^de_garbled_ix];
			aes_ciphertext = _mm_xor_si128(aes_ciphertext, Z[bit]);
			_mm_storeu_si128(reinterpret_cast<__m128i*>(&tmp[0]), aes_ciphertext);
			m_o_bufr.insert(m_o_bufr.end(), tmp.begin(), tmp.begin()+Env::key_size_in_bytes());

			// encrypt the 3rd entry : (X[1-x], Y[1-y])
			aes_key[0] = _mm_xor_si128(aes_key[0], m_R);

			KDF256((uint8_t*)&aes_plaintext, (uint8_t*)&aes_ciphertext, (uint8_t*)aes_key);
			aes_ciphertext = _mm_and_si128(aes_ciphertext, m_clear_mask);
			bit = current_gate.m_table[0x03^de_garbled_ix];
			aes_ciphertext = _mm_xor_si128(aes_ciphertext, Z[bit]);
			_mm_storeu_si128(reinterpret_cast<__m128i*>(&tmp[0]), aes_ciphertext);
			m_o_bufr.insert(m_o_bufr.end(), tmp.begin(), tmp.begin()+Env::key_size_in_bytes());
		}
		else // 1-arity gates
		{
			uint8_t bit;
			__m128i aes_key, aes_plaintext, aes_ciphertext;
			__m128i X[2], Z[2];
			static Bytes tmp;

			tmp.assign(16, 0);

			aes_plaintext = _mm_set1_epi64x(m_gate_ix);

			X[0] = _mm_load_si128(m_w+inputs[0]);
			X[1] = _mm_xor_si128(X[0], m_R);

			const uint8_t perm_x = _mm_extract_epi8(X[0], 0) & 0x01;

			// 0-th entry : X[x]
			aes_key = _mm_load_si128(X+perm_x);
			KDF128((uint8_t*)&aes_plaintext, (uint8_t*)&aes_ciphertext, (uint8_t*)&aes_key);
			aes_ciphertext = _mm_and_si128(aes_ciphertext, m_clear_mask);
			bit = current_gate.m_table[perm_x];

#ifdef GRR
			_mm_store_si128(Z+bit, aes_ciphertext);
			Z[1-bit] = _mm_xor_si128(Z[bit], m_R);
			current_zero_key = _mm_load_si128(Z);
#else
			tmp = m_prng.rand(Env::k());
			tmp.resize(16, 0);
			Z[0] = _mm_loadu_si128(reinterpret_cast<__m128i*>(&tmp[0]));
			Z[1] = _mm_xor_si128(Z[0], m_R);

			aes_ciphertext = _mm_xor_si128(aes_ciphertext, Z[bit]);
			_mm_storeu_si128(reinterpret_cast<__m128i*>(&tmp[0]), aes_ciphertext);
			m_o_bufr.insert(m_o_bufr.end(), tmp.begin(), tmp.begin()+Env::key_size_in_bytes());
#endif

			// 1-st entry : X[1-x]
			aes_key = _mm_xor_si128(aes_key, m_R);

			KDF128((uint8_t*)&aes_plaintext, (uint8_t*)&aes_ciphertext, (uint8_t*)&aes_key);
			aes_ciphertext = _mm_and_si128(aes_ciphertext, m_clear_mask);
			bit = current_gate.m_table[0x01^perm_x];
			aes_ciphertext = _mm_xor_si128(aes_ciphertext, Z[bit]);
			_mm_storeu_si128(reinterpret_cast<__m128i*>(&tmp[0]), aes_ciphertext);
			m_o_bufr.insert(m_o_bufr.end(), tmp.begin(), tmp.begin()+Env::key_size_in_bytes());
		}

		if (current_gate.m_tag == Circuit::EVL_OUT)
		{
			m_o_bufr.push_back(_mm_extract_epi8(current_zero_key, 0) & 0x01); // permutation bit
		}
		else if (current_gate.m_tag == Circuit::GEN_OUT)
		{
			m_o_bufr.push_back(_mm_extract_epi8(current_zero_key, 0) & 0x01); // permutation bit
		}
	}

	_mm_store_si128(m_w+current_gate.m_idx, current_zero_key);

	m_gate_ix++;
}

void GarbledCct3::update_hash(const Bytes &data)
{
	m_bufr += data;

#ifdef RAND_SEED
	if (m_bufr.size() > CIRCUIT_HASH_BUFFER_SIZE) // hash the circuit by chunks
	{
		m_hash.update(m_bufr);
		m_bufr.clear();
	}
#endif
}

void GarbledCct3::com_next_gate(const Gate &current_gate)
{
	gen_next_gate(current_gate);
	update_hash(m_o_bufr);
	m_o_bufr.clear(); // flush the output buffer
}

bool GarbledCct3::pass_check() const
{
	bool pass_chk = true;
	for (size_t ix = 0; ix < Env::circuit().gen_inp_cnt(); ix++)
	{
		pass_chk &= (m_gen_inp_decom[ix].hash(Env::k()) == m_gen_inp_com[ix]);
	}
	return pass_chk;
}

void GarbledCct3::evl_next_gate(const Gate &current_gate)
{
	__m128i current_key, a;
	Bytes::const_iterator it;
	static Bytes tmp;

	if (current_gate.m_tag == Circuit::GEN_INP)
	{
		uint8_t bit = m_gen_inp_mask.get_ith_bit(m_gen_inp_ix);
		Bytes::iterator it = m_i_bufr_ix + bit*Env::key_size_in_bytes();

		m_gen_inp_com[m_gen_inp_ix] = Bytes(it, it+Env::key_size_in_bytes());

		it = m_gen_inp_decom[m_gen_inp_ix].begin();
		tmp.assign(it, it+Env::key_size_in_bytes());
		tmp.resize(16, 0);
		current_key = _mm_loadu_si128(reinterpret_cast<__m128i*>(&tmp[0]));

		m_i_bufr_ix += Env::key_size_in_bytes()*2;
		m_gen_inp_ix++;
	}
	else if (current_gate.m_tag == Circuit::EVL_INP)
	{
		uint8_t bit = m_evl_inp.get_ith_bit(m_evl_inp_ix);
		Bytes::iterator it = m_i_bufr_ix + bit*Env::key_size_in_bytes();

		tmp = (*m_ot_keys)[m_evl_inp_ix];
		tmp.resize(16, 0);
		current_key = _mm_loadu_si128(reinterpret_cast<__m128i*>(&tmp[0]));

		tmp.assign(it, it+Env::key_size_in_bytes());
		tmp.resize(16, 0);
		a = _mm_loadu_si128(reinterpret_cast<__m128i*>(&tmp[0]));

		current_key = _mm_xor_si128(current_key, a);

		m_i_bufr_ix += Env::key_size_in_bytes()*2;
		m_evl_inp_ix++;
	}
	else
	{
        const vector<uint64_t> &inputs = current_gate.m_input_idx;

#ifdef FREE_XOR
		if (is_xor(current_gate))
		{
			current_key = inputs.size() == 2?
				_mm_xor_si128(m_w[inputs[0]], m_w[inputs[1]]) : _mm_load_si128(m_w+inputs[0]);
		}
		else
#endif
        if (inputs.size() == 2) // 2-arity gates
		{
        	__m128i aes_key[2], aes_plaintext, aes_ciphertext;

			aes_plaintext = _mm_set1_epi64x(m_gate_ix);

			aes_key[0] = _mm_load_si128(m_w+inputs[0]);
			aes_key[1] = _mm_load_si128(m_w+inputs[1]);

			const uint8_t perm_x = _mm_extract_epi8(aes_key[0], 0) & 0x01;
			const uint8_t perm_y = _mm_extract_epi8(aes_key[1], 0) & 0x01;

			KDF256((uint8_t*)&aes_plaintext, (uint8_t*)&aes_ciphertext, (uint8_t*)aes_key);
			aes_ciphertext = _mm_and_si128(aes_ciphertext, m_clear_mask);
			uint8_t garbled_ix = (perm_y<<1) | (perm_x<<0);

#ifdef GRR
			if (garbled_ix == 0)
			{
				current_key = _mm_load_si128(&aes_ciphertext);
			}
			else
			{
				it = m_i_bufr_ix+(garbled_ix-1)*Env::key_size_in_bytes();
				tmp.assign(it, it+Env::key_size_in_bytes());
				tmp.resize(16, 0);
				a = _mm_loadu_si128(reinterpret_cast<__m128i*>(&tmp[0]));
				current_key = _mm_xor_si128(aes_ciphertext, a);
			}
			m_i_bufr_ix += 3*Env::key_size_in_bytes();
#else
			it = m_i_bufr_ix + garbled_ix*Env::key_size_in_bytes();
			tmp.assign(it, it+Env::key_size_in_bytes());
			tmp.resize(16, 0);
			current_key = _mm_loadu_si128(reinterpret_cast<__m128i*>(&tmp[0]));
			current_key = _mm_xor_si128(current_key, aes_ciphertext);

			m_i_bufr_ix += 4*Env::key_size_in_bytes();
#endif
		}
		else // 1-arity gates
		{
        	__m128i aes_key, aes_plaintext, aes_ciphertext;

			aes_plaintext = _mm_set1_epi64x(m_gate_ix);
			aes_key = _mm_load_si128(m_w+inputs[0]);
			KDF128((uint8_t*)&aes_plaintext, (uint8_t*)&aes_ciphertext, (uint8_t*)&aes_key);
			aes_ciphertext = _mm_and_si128(aes_ciphertext, m_clear_mask);

			const uint8_t perm_x = _mm_extract_epi8(aes_key, 0) & 0x01;

#ifdef GRR
			if (perm_x == 0)
			{
				current_key = _mm_load_si128(&aes_ciphertext);
			}
			else
			{
				tmp.assign(m_i_bufr_ix, m_i_bufr_ix+Env::key_size_in_bytes());
				tmp.resize(16, 0);
				a = _mm_loadu_si128(reinterpret_cast<__m128i*>(&tmp[0]));
				current_key = _mm_xor_si128(aes_ciphertext, a);
			}
			m_i_bufr_ix += Env::key_size_in_bytes();
#else
			it = m_i_bufr_ix + garbled_ix*Env::key_size_in_bytes();
			tmp.assign(it, it+Env::key_size_in_bytes());
			tmp.resize(16, 0);
			current_key = _mm_loadu_si128(reinterpret_cast<__m128i*>(&tmp[0]));
			current_key = _mm_xor_si128(current_key, aes_ciphertext);

			m_i_bufr_ix += 2*Env::key_size_in_bytes();
#endif
		}

		if (current_gate.m_tag == Circuit::EVL_OUT)
		{
			uint8_t out_bit = _mm_extract_epi8(current_key, 0) & 0x01;
			out_bit ^= *m_i_bufr_ix;
			m_evl_out.set_ith_bit(m_evl_out_ix, out_bit);
			m_i_bufr_ix++;

			m_evl_out_ix++;
		}
		else if (current_gate.m_tag == Circuit::GEN_OUT)
		{
			// TODO: Ki08 implementation
			uint8_t out_bit = _mm_extract_epi8(current_key, 0) & 0x01;
			out_bit ^= *m_i_bufr_ix;
			m_gen_out.set_ith_bit(m_gen_out_ix, out_bit);
			m_i_bufr_ix++;

			m_gen_out_ix++;
		}
	}

	_mm_store_si128(m_w+current_gate.m_idx, current_key);

	update_hash(m_i_bufr);
	m_gate_ix++;
}

void GarbledCct3::gen_next_gen_inp_com(const Bytes &row, size_t kx)
{
	static Bytes tmp;

	__m128i out_key[2];
	tmp = m_prng.rand(Env::k());
	tmp.set_ith_bit(0, 0);
	tmp.resize(16, 0);
	out_key[0] = _mm_loadu_si128(reinterpret_cast<__m128i*>(&tmp[0]));
	out_key[1] = _mm_xor_si128(out_key[0], m_R);

	Bytes msg(m_gen_inp_decom[0].size());
	for (size_t jx = 0; jx < Env::circuit().gen_inp_cnt(); jx++)
	{
		if (row.get_ith_bit(jx))
		{
			byte bit = m_gen_inp_mask.get_ith_bit(jx);
			msg ^= m_gen_inp_decom[2*jx+bit];
			//msg ^= m_gen_inp_decom[2*jx];
		}
	}

	__m128i in_key[2], aes_plaintext, aes_ciphertext;

	aes_plaintext = _mm_set1_epi64x((uint64_t)kx);

	tmp.assign(msg.begin(), msg.begin()+Env::key_size_in_bytes());
	tmp.resize(16, 0);
	in_key[0] = _mm_loadu_si128(reinterpret_cast<__m128i*>(&tmp[0]));
	in_key[1] = _mm_xor_si128(in_key[0], m_R);

	KDF128((uint8_t*)&aes_plaintext, (uint8_t*)&aes_ciphertext, (uint8_t*)&in_key[0]);
	aes_ciphertext = _mm_and_si128(aes_ciphertext, m_clear_mask);
	out_key[0] = _mm_xor_si128(out_key[0], aes_ciphertext);

	KDF128((uint8_t*)&aes_plaintext, (uint8_t*)&aes_ciphertext, (uint8_t*)&in_key[1]);
	aes_ciphertext = _mm_and_si128(aes_ciphertext, m_clear_mask);
	out_key[1] = _mm_xor_si128(out_key[1], aes_ciphertext);

	const byte bit = msg.get_ith_bit(0);

	tmp.resize(16);
	_mm_storeu_si128(reinterpret_cast<__m128i*>(&tmp[0]), out_key[  bit]);
	m_o_bufr.insert(m_o_bufr.end(), tmp.begin(), tmp.begin()+Env::key_size_in_bytes());

	_mm_storeu_si128(reinterpret_cast<__m128i*>(&tmp[0]), out_key[1-bit]);
	m_o_bufr.insert(m_o_bufr.end(), tmp.begin(), tmp.begin()+Env::key_size_in_bytes());
//	tmp.resize(16);
//	_mm_storeu_si128(reinterpret_cast<__m128i*>(&tmp[0]), out_key[0]);
//	std::cout << "GEN " << m_gen_inp_hash_ix << " : (" << tmp.to_hex();
//
//	tmp.resize(16);
//	_mm_storeu_si128(reinterpret_cast<__m128i*>(&tmp[0]), out_key[1]);
//	std::cout << ", " << tmp.to_hex() << ")" << std::endl;

	m_gen_inp_hash_ix++;
}

void GarbledCct3::evl_next_gen_inp_com(const Bytes &row, size_t kx)
{
	Bytes out(m_gen_inp_decom[0].size());

	for (size_t jx = 0; jx < Env::circuit().gen_inp_cnt(); jx++)
	{
		if (row.get_ith_bit(jx)) { out ^= m_gen_inp_decom[jx]; }
	}

	byte bit = out.get_ith_bit(0);

	static Bytes tmp;

	Bytes::iterator it = m_i_bufr_ix + bit*Env::key_size_in_bytes();

	__m128i aes_key, aes_plaintext, aes_ciphertext, out_key;

	tmp.assign(out.begin(), out.begin()+Env::key_size_in_bytes());
	tmp.resize(16, 0);
	aes_key = _mm_loadu_si128(reinterpret_cast<__m128i*>(&tmp[0]));

	aes_plaintext = _mm_set1_epi64x((uint64_t)kx);

	KDF128((uint8_t*)&aes_plaintext, (uint8_t*)&aes_ciphertext, (uint8_t*)&aes_key);
	aes_ciphertext = _mm_and_si128(aes_ciphertext, m_clear_mask);

	tmp.assign(it, it+Env::key_size_in_bytes());
	tmp.resize(16, 0);
	out_key = _mm_loadu_si128(reinterpret_cast<__m128i*>(&tmp[0]));
	out_key = _mm_xor_si128(out_key, aes_ciphertext);

	bit = _mm_extract_epi8(out_key, 0) & 0x01;
	m_gen_inp_hash.set_ith_bit(kx, bit);

	m_i_bufr_ix += 2*Env::key_size_in_bytes();

//	tmp.resize(16);
//	_mm_storeu_si128(reinterpret_cast<__m128i*>(&tmp[0]), out_key);
//	std::cout << "EVL " << m_gen_inp_hash_ix << " : " << tmp.to_hex() << std::endl;

	m_gen_inp_hash_ix++;
}
