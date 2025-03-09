#include "assert.h"
#include "FurTexture.h"

FurTexture::FurTexture(void)
	:m_seed(30803)
	, m_size(128)
	, m_density(0.8f)
	, m_minLength(127)
{
}

void FurTexture::generate(void)
{
	assert((m_minLength >= 0) && (m_minLength <= 255));

	_generate();
}

void FurTexture::_generate()
{
	unsigned long maximumTextureCoord = m_size - 1;
	unsigned long numberOfStrands = unsigned long(float(m_size * m_size) * m_density);

	std::vector<uint8_t> textureData(m_size * m_size * 4, 0);

	DirectX::ScratchImage scratchImage;
	HRESULT hr = scratchImage.Initialize2D(
		DXGI_FORMAT_R8G8B8A8_UNORM,
		m_size,
		m_size,
		1,
		1
	);


	uint8_t* imageData = scratchImage.GetImages()->pixels;
	size_t rowPitch = scratchImage.GetImages()->rowPitch;


	for (unsigned long strand = 0; strand < numberOfStrands; ++strand)
	{
		unsigned long x = rand() % maximumTextureCoord;
		unsigned long y = rand() % maximumTextureCoord;
		unsigned long variableLength = 255 - m_minLength;
		unsigned long randomVariableLength = (variableLength <= 0) ? 0 : (rand() % variableLength);
		uint8_t alpha = m_minLength + randomVariableLength;

		textureData[(x * 4 * m_size) + (y * 4) + 3] = alpha; // figure out which one is really alpha.
	}

	for (uint32_t y = 0; y < m_size; ++y) {
		uint8_t* destRow = imageData + y * rowPitch; // Destination row in the ScratchImage buffer
		const uint8_t* srcRow = &textureData[y * m_size * 4]; // Source row in the input array

		memcpy(destRow, srcRow, m_size * 4); // Copy only the active pixels
	}

	hr = DirectX::SaveToDDSFile(
		scratchImage.GetImages(),
		scratchImage.GetImageCount(),
		scratchImage.GetMetadata(),
		DirectX::DDS_FLAGS_NONE,
		L"../Textures/furTex.dds"
	);
}
