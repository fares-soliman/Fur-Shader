#pragma once

#include "../Utilities/d3dUtil.h"
#include <DirectXTex.h>

class FurTexture
{
public:
	FurTexture();

	void generate();

private:
	unsigned long m_seed;
	unsigned long m_size;
	float m_density;
	unsigned long m_minLength;

	void _generate();
};
