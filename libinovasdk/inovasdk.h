// INOVASDK.h
#ifndef _INOVASDK_H
#define _INOVASDK_H

#define INOVASDK_VERSION 1.3.7 /*!< Current SDK version */

#define RESOLUTION_FULL 0 /*!< Full Resolution */
#define RESOLUTION_ROI 1 /*!< ROI resolution */
#define RESOLUTION_BIN 2 /*!< BIN 2x2 resolution */

#define FRAME_SPEED_LOW 0 /*!< Low Frame speed, 12bit depht, for long exposures */
#define FRAME_SPEED_NORMAL 1 /*!< Normal frame speed, 12bit depht, at higher frame rates */
#define FRAME_SPEED_HIGH 2 /*!< Maximum frame speed, 8bit depht, at full FPS */
#define FRAME_SPEED_TEST 5 /*!< Maximum frame speed, 12bit depht, at full FPS */

#define BAUD_RATE_9600 0 /*!< 9600 Bps, for UART */
#define BAUD_RATE_115200 1 /*!< 115200 Bps, for UART */

#define SENSOR_ID_NONE -1 /*!< No sensor at all */
#define SENSOR_ID_ICX618AL 0 /*!< SONY ICX618AL - Mono */
#define SENSOR_ID_ICX098BQ 1 /*!< SONY ICX098BQ - Color */
#define SENSOR_ID_ICX204AK 2 /*!< SONY ICX204AL - Mono */
#define SENSOR_ID_ICX445AL 3 /*!< SONY ICX445AL - Mono */
#define SENSOR_ID_ICX445AQ 4 /*!< SONY ICX445AQ - Color */
#define SENSOR_ID_ICX098BLE 5 /*!< SONY ICX098BLE - Color */
#define SENSOR_ID_ICX204AL 6 /*!< SONY ICX204AL - Mono */
#define SENSOR_ID_ICX205AK 7 /*!< SONY ICX205AK - Mono */
#define SENSOR_ID_ICX205AL 8 /*!< SONY ICX205AL - Mono */
#define SENSOR_ID_ICX674ALA 9 /*!< SONY ICX674ALA - Mono */
#define SENSOR_ID_ICX265ALA 10 /*!< SONY ICX265ALA - Mono */
#define SENSOR_ID_MT9M001 11 /*!< Aptina MT9M001 - Mono */
#define SENSOR_ID_MT9M034 12 /*!< Aptina MT9M034 - Color */
#define SENSOR_ID_MT9M034M 13 /*!< Aptina MT9M034M - Mono */
#define SENSOR_ID_IMX185 16 /*!< SONY IMX185 - Mono */

#define DIRECTION_NONE 0x0F, /*!< no direction, for ST4 */
#define DIRECTION_NORTH 0x0D, /*!< North direction, for ST4 */
#define DIRECTION_WEST 0x07, /*!< West direction, for ST4 */
#define DIRECTION_SOUTH 0x0B, /*!< South direction, for ST4 */
#define DIRECTION_EAST 0x0E /*!< East direction, for ST4 */

#define DATA_WIDTH_8 0 /*!< 8bpp, used for FRAME_SPEED_HIGH */
#define DATA_WIDTH_10 1 /*!< 10bpp, used for PLC-M */
#define DATA_WIDTH_12 2 /*!< 12/10bpp, used for FRAME_SPEED_NORMAL and FRAME_SPEED_LOW */

#define ROI_MIN_WIDTH		8
#define	ROI_MIN_HEIGHT		192
#define ROI_WIDTH_MASK		0xfff8
#define ROI_HEIGHT_MASK		0xfffe

#ifdef  __cplusplus
#define CXX_EXPORT extern "C"
#endif
#ifdef _WIN32
#define DLL_EXPORT CXX_EXPORT __declspec(dllexport)
#else
#define DLL_EXPORT CXX_EXPORT
#endif

typedef void* inova_ctx;

/*! \fn void iNovaSDK_MaxCamera(inova_ctx context)
 *  \brief Returns the number of cameras connected.
 */
DLL_EXPORT int iNovaSDK_MaxCamera();
/*! \fn void iNovaSDK_CloseCamera(inova_ctx context)
 *  \brief Closes the current camera.
 */
DLL_EXPORT void iNovaSDK_CloseCamera(inova_ctx context);
/*! \fn void iNovaSDK_OpenCamera(inova_ctx context, int n)
 *  \brief Detects and opens the selected camera.
 *  @param n: selected camera.
 *  @return Serial number
 */
DLL_EXPORT  const char * iNovaSDK_OpenCamera(inova_ctx *context, int n);
/*! \fn int iNovaSDK_SensorID(inova_ctx context)
 *  \brief Returns the camera's sensor code.
 *  @return INOVA_SENSOR_ID Sensor code.
 */
DLL_EXPORT  const char * iNovaSDK_SensorName(inova_ctx context);
/*! \fn int iNovaSDK_SensorName(inova_ctx context)
 *  \brief Returns the camera's sensor name.
 *  @return Sensor name.
 */
DLL_EXPORT int iNovaSDK_SensorID(inova_ctx context);
/*! \fn int iNovaSDK_GetArraySize(inova_ctx context)
 *  \brief Returns the size of the frame in pixels.
 *  @return Size current number of pixels.
 */
DLL_EXPORT int iNovaSDK_GetArraySize(inova_ctx context);
/*! \fn char * iNovaSDK_SerialNumber(inova_ctx context)
 *  \brief Returns the camera's serial number.
 *  @return serial number.
 */
DLL_EXPORT  const char * iNovaSDK_SerialNumber(inova_ctx context);
/*! \fn char * iNovaSDK_GetName(inova_ctx context)
 *  \brief Returns the camera's commercial name.
 *  @return Current camera's commercial name.
 */
DLL_EXPORT  const char * iNovaSDK_GetName(inova_ctx context);
/*! \fn char * iNovaSDK_RecvUartData(inova_ctx context)
 *  \brief Reads data from camera's serial port.
 *  @return Received data.
 */
DLL_EXPORT const char * iNovaSDK_RecvUartData(inova_ctx context);
/*! \fn bool iNovaSDK_SendUartData(inova_ctx context, unsigned char *Buffer, int len)
 *  \brief Sends data from camera's serial port.
 *  @param Buffer string to be sent.
 *  @param len Length of the string sent.
 *  @return true if success, false if error.
 */
DLL_EXPORT bool iNovaSDK_SendUartData(inova_ctx context, unsigned char *Buffer, int len);
/*! \fn void iNovaSDK_InitUart(inova_ctx context, int BaudRate)
 *  \brief Inits serial port.
 *  @param BaudRate speed of the serial port.
 *  @return no return value
 */
DLL_EXPORT void iNovaSDK_InitUart(inova_ctx context, int BaudRate);
/*! \fn bool iNovaSDK_InitCamera(inova_ctx context, int Resolution)
 *  \brief Inits the camera, in BIN mode resolution is halfed respect FULL resolution, but intensities are doubled.
 *  @param Resolution the resolution to use.
 *  @return true if success, false if error.
 */
DLL_EXPORT bool iNovaSDK_InitCamera(inova_ctx context, int resolution);
/*! \fn void iNovaSDK_OpenVideo(inova_ctx context)
 *  \brief Starts video capture, must be called after camera initialization.
 *  @return no return value
 */
DLL_EXPORT void iNovaSDK_OpenVideo(inova_ctx context);
/*! \fn void iNovaSDK_CloseVideo(inova_ctx context)
 *  \brief Stops video capture.
 *  @return no return value
 */
DLL_EXPORT void iNovaSDK_CloseVideo(inova_ctx context);
/*! \fn unsigned char * iNovaSDK_GrabFrame(inova_ctx context)
 *  \brief Returns the frame grabbed, or NULL if the frame is not ready. In FAST mode the frame is ordered in 8bit words, in NORMAL and LOW speed modes it is ordered in 16bit words (pay attention to the endianness!).
 *  @return the image array, or NULL.
 */
DLL_EXPORT unsigned char * iNovaSDK_GrabFrame(inova_ctx context);
/*! \fn void iNovaSDK_GetRowTime(inova_ctx context)
 *  \brief Returns the horizontal sync time in microseconds.
 *  @return the line time in microseconds.
 */
DLL_EXPORT double iNovaSDK_GetRowTime(inova_ctx context);
/*! \fn int iNovaSDK_GetExpTime(inova_ctx context)
 *  \brief Returns the exposure time in milliseconds.
 *  @return the total integration time.
 */
DLL_EXPORT double iNovaSDK_GetExpTime(inova_ctx context);
/*! \fn void iNovaSDK_SetExpTime(inova_ctx context, double x)
 *  \brief Sets exposure time.
 *  @param x Exposure time.
 *  @return no return value
 */
DLL_EXPORT void iNovaSDK_SetExpTime(inova_ctx context, double x);
/*! \fn void iNovaSDK_CancelLongExpTime(inova_ctx context)
 *  \brief Cancel current exposure.
 *  @return no return value
 */
DLL_EXPORT void iNovaSDK_CancelLongExpTime(inova_ctx context);
/*! \fn unsigned int iNovaSDK_GetAnalogGain(inova_ctx context)
 *  \brief Returns the analog gain value.
 *  @return Analog gain value
 */
DLL_EXPORT int iNovaSDK_GetAnalogGain(inova_ctx context);
/*! \fn void iNovaSDK_SetAnalogGain(inova_ctx context, int Gain)
 *  \brief Sets analog gain.
 *  @param Gain analog gain value.
 *  @return no return value
 */
DLL_EXPORT void iNovaSDK_SetAnalogGain(inova_ctx context, int AnalogGain);
/*! \fn void iNovaSDK_MaxImageHeight(inova_ctx context)
 *  \brief Returns the maximum Y resolution in pixels.
 *  @return Height max image height
 */
DLL_EXPORT int iNovaSDK_MaxImageHeight(inova_ctx context);
/*! \fn void iNovaSDK_MaxImageWidth(inova_ctx context)
 *  \brief Returns the maximum X resolution in pixels.
 *  @return Width max image width
 */
DLL_EXPORT int iNovaSDK_MaxImageWidth(inova_ctx context);
/*! \fn void iNovaSDK_GetImageHeight(inova_ctx context)
 *  \brief Returns the current Y resolution in pixels.
 *  @return Height image height
 */
DLL_EXPORT int iNovaSDK_GetImageHeight(inova_ctx context);
/*! \fn void iNovaSDK_GetImageWidth(inova_ctx context)
 *  \brief Returns the current X resolution in pixels.
 *  @return Height image width
 */
DLL_EXPORT int iNovaSDK_GetImageWidth(inova_ctx context);
/*! \fn void iNovaSDK_SetROI(inova_ctx context, int HOff, int VOff, int Width, int Height)
 *  \brief Sets ROI rectangle.
 *  @param HOff X start offset.
 *  @param VOff Y start offset.
 *  @param Width width of the rectangle in pixels.
 *  @param Height height of the rectangle in pixels.
 *  @return no return value
 */
DLL_EXPORT void iNovaSDK_SetROI(inova_ctx context, int HOff, int VOff, int Width, int Height);
/*! \fn int iNovaSDK_GetBinX(inova_ctx context)
 *  \brief Returns the X binning value.
 *  @return int BinX value.
 */
DLL_EXPORT int iNovaSDK_GetBinX(inova_ctx context);
/*! \fn void iNovaSDK_SetBinX(inova_ctx context, int bin)
 *  \brief Sets X binning value.
 *  @param bin BinX value.
 *  @return no return value
 */
DLL_EXPORT void iNovaSDK_SetBinX(inova_ctx context, int bin);
/*! \fn int iNovaSDK_GetBinY(inova_ctx context)
 *  \brief Returns the Y binning value.
 *  @return int BinY value.
 */
DLL_EXPORT int iNovaSDK_GetBinY(inova_ctx context);
/*! \fn void iNovaSDK_SetBinY(inova_ctx context, int bin)
 *  \brief Sets Y binning value.
 *  @param bin BinY value.
 *  @return no return value
 */
DLL_EXPORT void iNovaSDK_SetBinY(inova_ctx context, int bin);
/*! \fn int iNovaSDK_GetFrameSpeed(inova_ctx context)
 *  \brief Returns the frame speed.
 *  @return int speed frame speed.
 */
DLL_EXPORT int iNovaSDK_GetFrameSpeed(inova_ctx context);
/*! \fn void iNovaSDK_SetFrameSpeed(inova_ctx context, int speed)
 *  \brief Sets frame speed.
 *  @param speed Frame speed.
 *  @return no return value
 */
DLL_EXPORT void iNovaSDK_SetFrameSpeed(inova_ctx context, int speed);
/*! \fn unsigned char iNovaSDK_GetBlackLevel(inova_ctx context)
 *  \brief Returns the Black level.
 *  @return Black level value
 */
DLL_EXPORT int iNovaSDK_GetBlackLevel(inova_ctx context);
/*! \fn void iNovaSDK_SetBlackLevel(inova_ctx context, int Level)
 *  \brief Sets black level.
 *  @param Level value of black level.
 *  @return no return value
 */
DLL_EXPORT void iNovaSDK_SetBlackLevel(inova_ctx context, int Level);
/*! \fn int iNovaSDK_GetDataWide(inova_ctx context)
 *  \brief Returns the data width (bit depth).
 *  @return DATA_WIDTH_12 for 12/10bpp, DATA_WIDTH_8 for 8bpp
 */
DLL_EXPORT int iNovaSDK_GetDataWide(inova_ctx context);
/*! \fn void iNovaSDK_SetDataWide(inova_ctx context, int WordWidth)
 *  \brief Sets Bit depht.
 *  @param WordWidth bit depht.
 *  @return no return value
 */
DLL_EXPORT void iNovaSDK_SetDataWide(inova_ctx context, int WordWidth);
/*! \fn void iNovaSDK_SensorPowerDown(inova_ctx context)
 *  \brief Powers off the imaging sensor.
 *  @return no return value
 */
DLL_EXPORT void iNovaSDK_SensorPowerDown(inova_ctx context);
/*! \fn void iNovaSDK_SendST4(inova_ctx context, int value)
 *  \brief Sends direction to autoguider port.
 *  @param value direction for autoguider port.
 *  @return no return value
 */
DLL_EXPORT void iNovaSDK_SendST4(inova_ctx context, int value);
/*! \fn void iNovaSDK_InitST4(inova_ctx context)
 *  \brief Inits autoguider port.
 *  @return no return value
 */
DLL_EXPORT void iNovaSDK_InitST4(inova_ctx context);
/*! \fn void iNovaSDK_SetHB(inova_ctx context, int HB)
 *  \brief Sets horizontal blanking (CMOS sensors only).
 *  @param HB horizontal blanking.
 *  @return no return value
 */
DLL_EXPORT void iNovaSDK_SetHB(inova_ctx context, int pHB);
/*! \fn void iNovaSDK_SetVB(inova_ctx context, int VB)
 *  \brief Sets vertical blanking (CMOS sensors only).
 *  @param VB vertical blanking.
 *  @return no return value
 */
DLL_EXPORT void iNovaSDK_SetVB(inova_ctx context, int pVB);
/*! \fn void iNovaSDK_GetHB(inova_ctx context)
 *  \brief Returns the horizontal blanking (CMOS sensors only).
 *  @return horizontal blanking
 */
DLL_EXPORT int iNovaSDK_GetHB(inova_ctx context);
/*! \fn void iNovaSDK_GetVB(inova_ctx context)
 *  \brief Returns the vertical blanking (CMOS sensors only).
 *  @return vertical blanking
 */
DLL_EXPORT int iNovaSDK_GetVB(inova_ctx context);
/*! \fn void iNovaSDK_SetPixClock(inova_ctx context, int PixClock)
 *  \brief Sets pixel clock (Aptina MT9M034/MT9M034M sensors only).
 *  @param PixClock pixel clock.
 *  @return no return value
 */
DLL_EXPORT void iNovaSDK_SetPixClock(inova_ctx context, int Pck);
/*! \fn void iNovaSDK_GetPixClock(inova_ctx context)
 *  \brief Returns the pixel clock (Aptina MT9M034/MT9M034M sensors only).
 *  @return pixel clock
 */
DLL_EXPORT int iNovaSDK_GetPixClock(inova_ctx context);
/*! \fn void iNovaSDK_GetSensorTemperature(inova_ctx context)
 *  \brief Returns the sensor's temperature (Aptina MT9M034/MT9M034M sensors only).
 *  @return sensor's temperature
 */
DLL_EXPORT double iNovaSDK_GetSensorTemperature(inova_ctx context);
/*! \fn void iNovaSDK_HasST4(inova_ctx context)
 *  \brief Returns the availability of an autoguider port.
 *  @return true if ST4 port available, false if none
 */
DLL_EXPORT bool iNovaSDK_HasST4(inova_ctx context);
/*! \fn void iNovaSDK_HasUART(inova_ctx context)
 *  \brief Returns the availability of a serial port.
 *  @return true if serial port available, false if none
 */
DLL_EXPORT bool iNovaSDK_HasUART(inova_ctx context);
/*! \fn void iNovaSDK_HasColorSensor(inova_ctx context)
 *  \brief Returns the type of sensor.
 *  @return true if bayer color sensor, false if monochrome
 */
DLL_EXPORT bool iNovaSDK_HasColorSensor(inova_ctx context);
/*! \fn void iNovaSDK_GetPixelSizeX(inova_ctx context)
 *  \brief Returns the X size of the single pixel in microns.
 *  @return double X size of pixel
 */
DLL_EXPORT double iNovaSDK_GetPixelSizeX(inova_ctx context);
/*! \fn void iNovaSDK_GetPixelSizeY(inova_ctx context)
 *  \brief Returns the Y size of the single pixel in microns.
 *  @return double Y size of pixel
 */
DLL_EXPORT double iNovaSDK_GetPixelSizeY(inova_ctx context);
/*! \fn void iNovaSDK_GetLastFrameTime(inova_ctx context)
*  \brief Returns the milliseconds elapsed from Epoch to the last frame taken.
*/
DLL_EXPORT timeval iNovaSDK_GetLastFrameTime(inova_ctx context);

#endif
