/// @file CColorpalette.h
/// @brief Color palette manager for spectrograms with multiple color schemes
/// Provides RGB color generation from normalized values using various color maps

#pragma once

#include <vector>
#include "ColormapData.h"

/// @class CColorPalette
/// @brief Manages color palettes for spectrogram visualization
/// Generates RGB colors from float values in a specified range using various color schemes
class CColorPalette
{
public: 
	/// @brief Color scheme enumeration
	enum class PaletteName
	{
		kMono = 0,      ///< Monochrome (black and white)
		kBW,            ///< Black to White gradient
		kHot,           ///< Hot colormap (black->red->yellow->white)
		kRainbow,       ///< Rainbow spectrum
		kViridis,       ///< Viridis scientific colormap
		kPlasma,        ///< Plasma scientific colormap
		kJade           ///< Jade custom colormap
	};

	/// @brief Default constructor - initializes with monochrome 2-color palette
	CColorPalette();
	
	/// @brief Constructor with specified number of colors
	/// @param NrOfColors Number of discrete colors in palette
	CColorPalette(int NrOfColors);
	
	/// @brief Constructor with specified colors and color scheme
	/// @param NrOfColors Number of discrete colors in palette
	/// @param ColorScheme Color scheme enumeration value (default: PaletteName::kRainbow)
	CColorPalette(int NrOfColors, PaletteName ColorScheme=PaletteName::kRainbow);
	
	/// @brief Destructor
	~CColorPalette();
	
	/// @name Setter methods
	///@{
	/// @brief Set the value range for color mapping
	/// @param Min Minimum value in the range
	/// @param Max Maximum value in the range
	void setValueRange (float Min, float Max);
	
	/// @brief Set the number of discrete colors in the palette
	/// @param NrOfColors Number of color steps
	void setNrOfColors (int NrOfColors);
	
	/// @brief Set the color scheme
	/// @param ColorScheme Color scheme enumeration value
	void setColorScheme (PaletteName ColorScheme);
	
	/// @brief Toggle color scheme inversion
	/// @param status True to invert, false for normal
	void setInvertStatus(bool status){m_InvertScheme = status;};
	///@}

	/// @name Accessor methods
	///@{
	/// @brief Get RGB color for a normalized value with clamping and saturation
	/// @param value Input value in range [m_Min, m_Max]
	/// @return 32-bit RGB color in format 0xRRGGBB
	inline int getRGBColor(float value)
	{
	if (value >= m_Max)
		value = m_Max*0.9999f;

	if (value < m_Min)
		value = m_Min;

	int index = static_cast<int> ((value-m_Min) * m_AccessMult);

	if (index < m_NrOfColors)
		return m_Color[index];
	else
		return m_Color[m_NrOfColors-1];

	}; 
	
	/// @brief Get the normalized value corresponding to an RGB color
	/// @param iColor RGB color in format 0xRRGGBB
	/// @return Value in range [m_Min, m_Max] or very large number if not found
	float getValue(int iColor);
	///@}


protected:
	/// @brief Allocate and compute all palette colors
	void ComputeColors(void);
	
	/// @brief Resize color vector and regenerate palette
	void AllocateColors(void);
	
	std::vector<int> m_Color;       ///< Array of RGB colors (0xRRGGBB format)
	int m_NrOfColors;               ///< Number of discrete colors in palette
	float m_Max;                    ///< Maximum value for color mapping
	float m_Min;                    ///< Minimum value for color mapping
	float m_AccessMult;             ///< Multiplier for value-to-index conversion
	PaletteName m_ColorScheme;      ///< Current color scheme (enum value)
	int m_InvertScheme;             ///< Flag for scheme inversion (0=normal, 1=inverted)


};
