#pragma once

#include "pscx_common.h"
#include "pscx_memory.h"
#include "pscx_renderer.h"
#include "pscx_timekeeper.h"
#include "pscx_interrupts.h"
#include "pscx_timers.h"

//const Cycles CLOCK_RATIO_FRAC = 0x10000;

// Depth of the pixel values in a texture page
enum TextureDepth
{
	// 4 bits per pixel
	TEXTURE_DEPTH_4_BIT,

	// 8 bits per pixel
	TEXTURE_DEPTH_8_BIT,

	// 15 bits per pixel
	TEXTURE_DEPTH_15_BIT
};

// Interlaced output splits each frame in two fields
enum Field
{
	// Top Field ( odd lines )
	FIELD_TOP = 1,

	// Bottom Field ( even lines )
	FIELD_BOTTOM = 0
};

// Video output horizontal resolution
struct HorizontalRes
{
	HorizontalRes(uint8_t hr) : m_horizontalRes(hr) {}

	// Create a new HorizontalRes instance from 2 bit field 'hr1' and the one bit field 'hr2'
	static HorizontalRes createFromFields(uint8_t hr1, uint8_t hr2)
	{
		return HorizontalRes((hr2 & 1) | ((hr1 & 3) << 1));
	}

	// Retrieve value of bits [18:16] of the status register
	uint32_t intoStatus() const
	{
		return ((uint32_t)m_horizontalRes) << 16;
	}

	// Return the divider used to generate the dotclock from the GPU clock.
	uint8_t dotclockDivider()
	{
		uint8_t hr1 = (m_horizontalRes >> 1) & 0x3;
		uint8_t hr2 = m_horizontalRes & 1;

		// If bit "Horizontal Resolution 2" is set then we're in "368 pixel"
		// mode (dotclock = GPU clock / 7). If it's not set then we must check
		// the other two bits of "Horizontal Resolution 2".

		// Note that the horizontal resolutions given here are estimates,
		// it's roughly the number of dotclock ticks necessary to fill a line with a given
		// divider. "displayHorizStart" and "displayHorizEnd" will give the actual resoultion.
		uint8_t dotclockDivider = 1;
		if (hr2)
		{
			// HRes ~ 368 pixels
			dotclockDivider = 7;
		}
		else
		{
			switch (hr1)
			{
			// Hres ~ 256 pixels
			case 0:
				dotclockDivider = 10;
				break;
			// Hres ~ 320 pixels
			case 1:
				dotclockDivider = 8;
				break;
			// Hres ~ 512 pixels
			case 2:
				dotclockDivider = 5;
				break;
			// Hres ~ 640 pixels
			case 3:
				dotclockDivider = 4;
				break;
			}
		}
		return dotclockDivider;
	}

	uint8_t m_horizontalRes;
};

// Video output vertical resolution
enum VerticalRes
{
	// 240 lines
	VERTICAL_RES_240_LINES,

	// 480 lines ( only available for interlaced output )
	VERTICAL_RES_480_LINES
};

// Video modes
enum VMode
{
	// NTSC: 480i60Hz
	VMODE_NTSC,

	// PAL: 576i50Hz
	VMODE_PAL
};

// Display area color depth
enum DisplayDepth
{
	// 15 bits per pixel
	DISPLAY_DEPTH_15_BITS,

	// 24 bits per pixel
	DISPLAY_DEPTH_24_BITS
};

// Requested DMA direction
enum DmaDirection
{
	DMA_DIRECTION_OFF,
	DMA_DIRECTION_FIFO,
	DMA_DIRECTION_CPU_TO_GP0,
	DMA_DIRECTION_VRAM_TO_CPU
};

// There are a few hardware differences between PAL and NTSC consoles,
// for instance runs slightly slower on PAL consoles.
enum HardwareType
{
	HARDWARE_TYPE_NTSC,
	HARDWARE_TYPE_PAL
};

// Buffer holding multi-word fixed-length GP0 command parameters
struct CommandBuffer
{
	CommandBuffer()
	{
		memset(m_buffer, 0x0, sizeof(m_buffer));
		m_len = 0x0;
	}

	// Clear the command buffer
	void clear()
	{
		m_len = 0x0;
	}

	void pushWord(uint32_t word)
	{
		if (m_len >= _countof(m_buffer)) return;

		m_buffer[m_len] = word;
		m_len++;
	}

	uint32_t& operator[](size_t idx)
	{
		return m_buffer[idx];
	}

private:
	// Command buffer: the longuest possible command is GP0(0x3e) which takes 12 parameters
	uint32_t m_buffer[12];

	// Number of words queued in buffer
	uint8_t m_len;
};

// Possible states for the GP0 command register
enum Gp0Mode
{
	// Default mode: handling commands
	GP0_MODE_COMMAND,

	// Loading an image into VRAM
	GP0_MODE_IMAGE_LOAD
};

struct Gpu
{
	Gpu(HardwareType hardwareType) :
		m_pageBaseX(0x0),
		m_pageBaseY(0x0),
		m_rectangleTextureXFlip(false),
		m_rectangleTextureYFlip(false),
		m_semiTransparency(0x0),
		m_textureDepth(TextureDepth::TEXTURE_DEPTH_4_BIT),
		m_textureWindowXMask(0x0),
		m_textureWindowYMask(0x0),
		m_textureWindowXOffset(0x0),
		m_textureWindowYOffset(0x0),
		m_dithering(false),
		m_drawToDisplay(false),
		m_forceSetMaskBit(false),
		m_preserveMaskedPixels(false),
		m_drawingAreaLeft(0x0),
		m_drawingAreaTop(0x0),
		m_drawingAreaRight(0x0),
		m_drawingAreaBottom(0x0),
		m_field(Field::FIELD_TOP),
		m_textureDisable(false),
		m_hres(HorizontalRes::createFromFields(0x0, 0x0)),
		m_vres(VerticalRes::VERTICAL_RES_240_LINES),
		m_vmode(VMode::VMODE_NTSC),
		m_displayDepth(DisplayDepth::DISPLAY_DEPTH_15_BITS),
		m_interlaced(false),
		m_displayDisabled(true),
		m_displayVramXStart(0x0),
		m_displayVramYStart(0x0),
		m_displayHorizStart(0x200),
		m_displayHorizEnd(0xc00),
		m_displayLineStart(0x10),
		m_displayLineEnd(0x100),
		//m_interrupt(false),
		m_dmaDirection(DmaDirection::DMA_DIRECTION_OFF),
		m_gp0WordsRemaining(0x0),
		m_gp0CommandMethod(&Gpu::gp0Nop),
		m_gp0Mode(Gp0Mode::GP0_MODE_COMMAND),
		m_gp0Interrupt(false),
		m_vblankInterrupt(false),
		//m_gpuClockFrac(0x0),
		m_gpuClockPhase(0x0),
		m_displayLine(0x0),
		m_displayLineTick(0x0),
		m_hardwareType(hardwareType)
	{}

	// Return the number of GPU clock cycles in a line and number of
	// lines in a frame (or field for interlaced output) depending on
	// the configured video mode
	std::pair<uint16_t, uint16_t> getVModeTimings() const;

	// Return the GPU to CPU clock ratio. The value is multiplied by
	// CLOCK_RATIO_FRAC to get a precise fixed point value
	FracCycles gpuToCpuClockRatio() const;

	// Return the period of the dotclock expressed in CPU block periods
	FracCycles dotclockPeriod();

	// Return the current phase of the GPU dotclock relative to the CPU clock
	FracCycles dotclockPhase();

	// Return the period of the HSync signal in CPU clock periods
	FracCycles hsyncPeriod();

	// Return the phase of the hsync (position within the line) in
	// CPU clock periods
	FracCycles hsyncPhase();

	// Update the GPU state to its current status
	void sync(TimeKeeper& timeKeeper, InterruptState& irqState);

	// Predict when the next "forced" sync should take place
	void predictNextSync(TimeKeeper timeKeeper);

	// Return true if we're currently in the video blanking period
	bool inVblank() const;

	// Return the index of the currently displayed VRAM line
	uint16_t displayedVramLine() const;

	template<typename T>
	T load(TimeKeeper& timeKeeper, InterruptState& irqState, uint32_t offset);

	template<typename T>
	void store(TimeKeeper& timeKeeper, Timers& timers, InterruptState& irqState, uint32_t offset, T value);

	// Retrieve value of the status register
	uint32_t getStatusRegister() const;

	// Retrieve value of the read register
	uint32_t getReadRegister() const;

	// Handle writes to the GP0 command register
	void gp0(uint32_t value);

	// Handle writes to the GP1 command register
	void gp1(uint32_t value, TimeKeeper& timeKeeper, Timers& timers, InterruptState& irqState);

	// GP0(0x0): NOP
	void gp0Nop();

	// GP0(0x01): Clear cache
	void gp0ClearCache();

	// GP0(0x02): Fill Rectangle
	void gp0FillRect();

	// GP0(0x28): Monochrome Opaque Quadrilateral
	void gp0QuadMonoOpaque();

	// GP0(0x2c): Textured-Blended Opaque Quadrilateral
	void gp0QuadTextureBlendOpaque();

	// GP0(0x2d): Raw Textured Opaque Quadrilateral
	void gp0QuadTextureRawOpaque();

	// GP0(0x30): Shaded Opaque Triangle
	void gp0TriangleShadedOpaque();

	// GP0(0x38): Shaded Opaque Quadrilateral
	void gp0QuadShadedOpaque();

	// GP0(0x65): Opaque rectangle with raw texture
	void gp0RectTextureRawOpaque();

	// GP0(0xa0): Image Load
	void gp0ImageLoad();

	// GP0(0xc0): Image Store
	void gp0ImageStore();

	// GP0(0xe1) command
	void gp0DrawMode();

	// GP0(0xe2): Set Texture Window
	void gp0TextureWindow();

	// GP0(0xe3): Set Drawing Area top left
	void gp0DrawingAreaTopLeft();

	// GP0(0xe4): Set Drawing Area bottom right
	void gp0DrawingAreaBottomRight();

	// GP0(0xe5): Set Drawing Offset
	void gp0DrawingOffset();

	// GP0(0xe6): Set Mask Bit Setting
	void gp0MaskBitSetting();

	// GP1(0x0): Soft reset
	void gp1Reset(TimeKeeper& timeKeeper, InterruptState& irqState);

	// Gp1(0x01): Reset Command Buffer
	void gp1ResetCommandBuffer();

	// GP1(0x02): Acknowledge Interrupt
	void gp1AcknowledgeIrq();

	// GP1(0x03): Display Enable
	void gp1DisplayEnable(uint32_t value);

	// GP1(0x04): DMA direction
	void gp1DmaDirection(uint32_t value);

	// GP1(0x05): Display VRAM Start
	void gp1DisplayVramStart(uint32_t value);

	// GP1(0x06): Display Horizontal Range
	void gp1DisplayHorizontalRange(uint32_t value);

	// GP1(0x07): Display Vertical Range
	void gp1DisplayVerticalRange(uint32_t value, TimeKeeper& timeKeeper, InterruptState& irqState);

	// GP1(0x08): Display Mode
	void gp1DisplayMode(uint32_t value, TimeKeeper& timeKeeper, InterruptState& irqState);

private:
	// Texture page base X coordinate ( 4 bits, 64 byte increment )
	uint8_t m_pageBaseX;

	// Texture page base Y coordinate ( 1 bit, 256 line increment )
	uint8_t m_pageBaseY;

	// Semi-transparency
	uint8_t m_semiTransparency;

	// Texture page color depth
	TextureDepth m_textureDepth;

	// Enable dithering from 24 to 15 bits RGB
	bool m_dithering;

	// Allow drawing to the display area
	bool m_drawToDisplay;

	// Force "mask" bit of the pixel to 1 when writing to VRAM
	// ( otherwise don't modify it )
	bool m_forceSetMaskBit;

	// Don't draw to pixels which have the "mask" bit set
	bool m_preserveMaskedPixels;

	// Currently displayed field. For progressive output this is always Top.
	Field m_field;

	// When true all textures are disabled
	bool m_textureDisable;

	// Video output horizontal resolution
	HorizontalRes m_hres;

	// Video output vertical resolution
	VerticalRes m_vres;

	// Video mode
	VMode m_vmode;

	// Display depth. The GPU itself always draws 15 bit RGB, 24 bit output must use external assets
	// ( pre-rendered textures, MDEC, etc... )
	DisplayDepth m_displayDepth;

	// Output interlaced video signal instead of progressive
	bool m_interlaced;

	// Disable the display
	bool m_displayDisabled;

	// True when the interrupt is active
	// bool m_interrupt;

	// DMA request direction
	DmaDirection m_dmaDirection;

	// Mirror textured rectangles along the x axis
	bool m_rectangleTextureXFlip;

	// Mirror textured rectangles along the y axis
	bool m_rectangleTextureYFlip;

	// GP1

	// Texture window x mask ( 8 pixel steps )
	uint8_t m_textureWindowXMask;

	// Texture window y mask ( 8 pixel steps )
	uint8_t m_textureWindowYMask;

	// Texture window x offset ( 8 pixel steps )
	uint8_t m_textureWindowXOffset;

	// Texture window y offset ( 8 pixel steps )
	uint8_t m_textureWindowYOffset;

	// Left-most column of drawing area
	uint16_t m_drawingAreaLeft;

	// Top-most line of drawing area
	uint16_t m_drawingAreaTop;

	// Right-most column of drawing area
	uint16_t m_drawingAreaRight;

	// Bottom-most line of drawing area
	uint16_t m_drawingAreaBottom;

	// First column of the display area in VRAM
	uint16_t m_displayVramXStart;

	// First line of the display area in VRAM
	uint16_t m_displayVramYStart;

	// Display output horizontal start relative to HSYNC
	uint16_t m_displayHorizStart;

	// Display output horizontal end relative to HSYNC
	uint16_t m_displayHorizEnd;

	// Display output first line relative to VSYNC
	uint16_t m_displayLineStart;

	// Display output last line relative to VSYNC
	uint16_t m_displayLineEnd;

	// Buffer containing the current GP0 command
	CommandBuffer m_gp0Command;

	// Remaining words for the current GP0 command
	uint32_t m_gp0WordsRemaining;

	// Pointer to the method implementing the current GP0 command
	void (Gpu::*m_gp0CommandMethod)(void) = nullptr;

	// Current mode of the GP0 register
	Gp0Mode m_gp0Mode;

	// OpenGL renderer
	Renderer m_renderer;

	// True when the GP0 interrupt has been requested
	bool m_gp0Interrupt;

	// True when the VBLANK interrupt is high
	bool m_vblankInterrupt;

	// Fractional GPU cycle remainder resulting from the CPU
	// clock/GPU clock time conversion.
	// uint16_t m_gpuClockFrac;

	// Clock/GPU clock time conversion. Effectively the phase of the
	// GPU clock relative to the CPU, expressed in CPU clock periods.
	uint16_t m_gpuClockPhase;

	// Currently displayed video output line
	uint16_t m_displayLine;

	// Current GPU clock tick for the current line
	uint16_t m_displayLineTick;

	// Hardware type (PAL or NTSC)
	HardwareType m_hardwareType;
};
