/*
    indi_interferometer - a telescope array driver for INDI
    Support for AHP cross-correlators
    Copyright (C) 2020  Ilia Platone

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <stdlib.h>
#include <termios.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/file.h>
#include <memory>
#include <regex>
#include <indicom.h>
#include <sys/stat.h>

#include <libnova/sidereal_time.h>
#include <libnova/transform.h>
#include <libnova/julian_day.h>

#include "indi_ahp_xc.h"

static unsigned int nplots = 1;
static std::unique_ptr<AHP_XC> array(new AHP_XC());

void* AHP_XC::createFITS(int bpp, size_t *memsize, dsp_stream_p stream)
{
    int img_type  = USHORT_IMG;
    int byte_type = TUSHORT;
    std::string bit_depth = "16 bits per sample";
    switch (bpp)
    {
        case 8:
            byte_type = TBYTE;
            img_type  = BYTE_IMG;
            bit_depth = "8 bits per sample";
            break;

        case 16:
            byte_type = TUSHORT;
            img_type  = USHORT_IMG;
            bit_depth = "16 bits per pixel";
            break;

        case 32:
            byte_type = TUINT;
            img_type  = ULONG_IMG;
            bit_depth = "32 bits per sample";
            break;

        case 64:
            byte_type = TLONG;
            img_type  = ULONG_IMG;
            bit_depth = "64 bits double per sample";
            break;

        case -32:
            byte_type = TFLOAT;
            img_type  = FLOAT_IMG;
            bit_depth = "32 bits double per sample";
            break;

        case -64:
            byte_type = TDOUBLE;
            img_type  = DOUBLE_IMG;
            bit_depth = "64 bits double per sample";
            break;

        default:
            DEBUGF(INDI::Logger::DBG_ERROR, "Unsupported bits per sample value %d", getBPS());
            return nullptr;
    }
    uint8_t *memptr = nullptr;
    fitsfile *fptr = nullptr;
    int status = 0;
    int naxis;
    long *naxes;
    long nelements = stream->len;
    void *buf = malloc(stream->len * fabs(bpp) / 8);
    switch (bpp)
    {
        case 8:
            dsp_buffer_copy(stream->buf, (static_cast<uint8_t *>(buf)), stream->len);
            break;
        case 16:
            dsp_buffer_copy(stream->buf, (static_cast<uint16_t *>(buf)), stream->len);
            break;
        case 32:
            dsp_buffer_copy(stream->buf, (static_cast<uint32_t *>(buf)), stream->len);
            break;
        case 64:
            dsp_buffer_copy(stream->buf, (static_cast<unsigned long *>(buf)), stream->len);
            break;
        case -32:
            dsp_buffer_copy(stream->buf, (static_cast<float *>(buf)), stream->len);
            break;
        case -64:
            dsp_buffer_copy(stream->buf, (static_cast<double *>(buf)), stream->len);
            break;
        default:
            free (buf);
            break;
    }
    naxis = stream->dims;
    naxes = (long*)malloc(sizeof(long) * stream->dims);
    for(int d = 0; d < stream->dims; d++)
        naxes[d] = stream->sizes[d];

    char error_status[MAXINDINAME];

    //  Now we have to send fits format data to the client
    *memsize = 5760;
    memptr  = static_cast<uint8_t*>(malloc(*memsize));
    if (!memptr)
    {
        LOGF_ERROR("Error: failed to allocate memory: %lu", *memsize);
        return nullptr;
    }

    fits_create_memfile(&fptr, ((void**)&memptr), memsize, 2880, realloc, &status);

    if (status)
    {
        fits_report_error(stderr, status);
        fits_get_errstatus(status, error_status);
        fits_close_file(fptr, &status);
        free(memptr);
        LOGF_ERROR("FITS Error: %s", error_status);
        return nullptr;
    }

    fits_create_img(fptr, img_type, naxis, naxes, &status);

    if (status)
    {
        fits_report_error(stderr, status);
        fits_get_errstatus(status, error_status);
        fits_close_file(fptr, &status);
        free(memptr);
        LOGF_ERROR("FITS Error: %s", error_status);
        return nullptr;
    }

    addFITSKeywords(fptr, memptr, *memsize);

    fits_write_img(fptr, byte_type, 1, nelements, buf, &status);
    free(buf);

    if (status)
    {
        fits_report_error(stderr, status);
        fits_get_errstatus(status, error_status);
        fits_close_file(fptr, &status);
        free(memptr);
        LOGF_ERROR("FITS Error: %s", error_status);
        return nullptr;
    }
    fits_close_file(fptr, &status);
    return memptr;
}

void AHP_XC::Callback()
{
    threadsRunning = true;
    while (threadsRunning)
    {
        if(!isConnected())
        {
            usleep(100000);
            continue;
        }
        ahp_xc_packet* packet = ahp_xc_alloc_packet();
        if(ahp_xc_get_packet(packet))
        {
            usleep(ahp_xc_get_packettime());
            continue;
        }
        timeleft = CalcTimeLeft();
        int idx = 0;

        if(timeleft <= 0.0)
            timeleft = 0;

        if(InIntegration)
        {
            if(timeleft == 0.0)
            {
                // We're no longer integrating
                InIntegration = false;
                // We're done exposing
                LOG_INFO("Integration complete, downloading plots...");
                // Additional BLOBs
                size_t memsize = 0;
                char* blob = static_cast<char*>(malloc(1));
                for(unsigned int x = 0; x < nplots; x++)
                {
                    if(HasDSP())
                    {
                        DSP->processBLOB(static_cast<unsigned char*>(static_cast<void*>(plot_str[x]->buf)), static_cast<unsigned int>(plot_str[x]->dims), plot_str[x]->sizes, -64); //TODO
                    }
                    size_t next_size = static_cast<unsigned int>(plot_str[x]->len) * sizeof(double);
                    blob = static_cast<char*>(realloc(blob, memsize+next_size));
                    void* fits = createFITS(-64, &memsize, plot_str[x]);
                    if(fits != nullptr)
                    {
                        memcpy(&blob[memsize], fits, next_size);
                        plotB[x].blob = &blob[memsize];
                        plotB[x].bloblen = static_cast<int>(next_size);
                        memsize += next_size;
                        free(fits);
                    }
                    dsp_buffer_set(plot_str[x]->buf, plot_str[x]->len, 0);
                }
                plotBP.s = IPS_OK;
                LOG_INFO("Plots BLOBs generated, downloading...");
                IDSetBLOB(&plotBP, nullptr);
                memsize = 0;
                if(ahp_xc_get_nlines() > 0 && ahp_xc_get_autocorrelator_lagsize() > 1)
                {
                    LOG_INFO("Generating autocorrelation BLOBs...");
                    for(unsigned int x = 0; x < ahp_xc_get_nlines(); x++)
                    {
                        size_t next_size = static_cast<unsigned int>(autocorrelations_str[x]->len) * sizeof(double);
                        blob = static_cast<char*>(realloc(blob, memsize+next_size));
                        void* fits = createFITS(-64, &memsize, autocorrelations_str[x]);
                        if(fits != nullptr)
                        {
                            memcpy(&blob[memsize], fits, next_size);
                            autocorrelationsB[x].blob = &blob[memsize];
                            autocorrelationsB[x].bloblen = static_cast<int>(next_size);
                            memsize += next_size;
                            free(fits);
                        }
                        dsp_stream_set_dim(autocorrelations_str[x], 1, 1);
                        dsp_stream_alloc_buffer(autocorrelations_str[x], autocorrelations_str[x]->len);
                        dsp_buffer_set(autocorrelations_str[x]->buf, autocorrelations_str[x]->len, 0);
                    }
                    autocorrelationsBP.s = IPS_OK;
                    LOG_INFO("Autocorrelations BLOBs generated, downloading...");
                    IDSetBLOB(&autocorrelationsBP, nullptr);
                }
                memsize = 0;
                if(ahp_xc_get_nbaselines() > 0 && ahp_xc_get_crosscorrelator_lagsize() > 1)
                {
                    LOG_INFO("Generating crosscorrelation BLOBs...");
                    idx = 0;
                    for(unsigned int x = 0; x < ahp_xc_get_nlines(); x++)
                    {
                        for(unsigned int y = x + 1; y < ahp_xc_get_nlines(); y++)
                        {
                            size_t next_size = static_cast<unsigned int>(crosscorrelations_str[x]->len) * sizeof(double);
                            blob = static_cast<char*>(realloc(blob, memsize+next_size));
                            void* fits = createFITS(-64, &memsize, crosscorrelations_str[x]);
                            if(fits != nullptr)
                            {
                                memcpy(&blob[memsize], fits, next_size);
                                crosscorrelationsB[x].blob = &blob[memsize];
                                crosscorrelationsB[x].bloblen = static_cast<int>(next_size);
                                memsize += next_size;
                                free(fits);
                            }
                            dsp_stream_set_dim(crosscorrelations_str[x], 1, 1);
                            dsp_stream_alloc_buffer(crosscorrelations_str[x], crosscorrelations_str[x]->len);
                            dsp_buffer_set(crosscorrelations_str[x]->buf, crosscorrelations_str[x]->len, 0);
                        }
                    }
                    crosscorrelationsBP.s = IPS_OK;
                    LOG_INFO("Crosscorrelations BLOBs generated, downloading...");
                    IDSetBLOB(&crosscorrelationsBP, nullptr);
                }
                free(blob);
                FramedIntegrationNP.s = IPS_OK;
                IDSetNumber(&FramedIntegrationNP, nullptr);
                LOG_INFO("Download complete.");
            }
            else
            {
                double lst = get_local_sidereal_time(Longitude);
                double ha = get_local_hour_angle(lst, RA);
                get_alt_az_coordinates(ha * 15, Dec, Latitude, &Altitude, &Azimuth);

                double center_tmp[3] = {0, 0, 0};
                int first = -1;
                idx = 1;
                for(unsigned int x = 0; x < ahp_xc_get_nlines(); x++)
                {
                    if(lineEnableSP[x].sp[0].s == ISS_ON)
                    {
                        if(first > -1)
                        {
                            center_tmp[0] += lineLocationNP[x].np[0].value - lineLocationNP[first].np[0].value;
                            center_tmp[1] += lineLocationNP[x].np[1].value - lineLocationNP[first].np[1].value;
                            center_tmp[2] += lineLocationNP[x].np[2].value - lineLocationNP[first].np[2].value;
                            idx++;
                        }
                        else
                        {
                            first = static_cast<int>(x);
                        }
                    }
                }
                center_tmp[0] /= idx;
                center_tmp[1] /= idx;
                center_tmp[2] /= idx;
                center_tmp[0] += lineLocationNP[first].np[0].value;
                center_tmp[1] += lineLocationNP[first].np[1].value;
                center_tmp[2] += lineLocationNP[first].np[2].value;
                unsigned int farest = 0;
                double delay_max = 0;
                for(unsigned int x = 0; x < ahp_xc_get_nlines(); x++)
                {
                    if(lineEnableSP[x].sp[0].s == ISS_ON)
                    {
                        center[x].x = lineLocationNP[x].np[0].value - center_tmp[0];
                        center[x].y = lineLocationNP[x].np[1].value - center_tmp[1];
                        center[x].z = lineLocationNP[x].np[2].value - center_tmp[2];
                        double delay_tmp = baseline_delay(Altitude, Azimuth, center[x].values) / sqrt(pow(center[x].x, 2) + pow(center[x].y,
                                           2) + pow(center[x].z, 2));
                        farest = (delay_tmp > delay_max ? x : farest);
                        delay_max = (delay_tmp > delay_max ? delay_tmp : delay_max);
                    }
                }
                delay[farest] = 0;
                ahp_xc_set_channel_auto(static_cast<unsigned int>(farest), 0, 1, 1);
                ahp_xc_set_channel_cross(static_cast<unsigned int>(farest), 0, 1, 1);
                idx = 0;
                for(unsigned int x = 0; x < ahp_xc_get_nlines(); x++)
                {
                    for(unsigned int y = x + 1; y < ahp_xc_get_nlines(); y++)
                    {
                        if((lineEnableSP[x].sp[0].s == ISS_ON) && lineEnableSP[y].sp[0].s == ISS_ON)
                        {
                            double d = fabs(baselines[idx]->getDelay(Altitude, Azimuth));
                            unsigned int delay_clocks = d * ahp_xc_get_frequency() / LIGHTSPEED;
                            delay_clocks = (delay_clocks > 0 ? (delay_clocks < ahp_xc_get_delaysize() ? delay_clocks : ahp_xc_get_delaysize() - 1) : 0);
                            if(y == farest)
                            {
                                delay[x] = d;
                                ahp_xc_set_channel_auto(x, 0, 1, 1);
                                ahp_xc_set_channel_cross(x, delay_clocks, 1, 1);
                            }
                            if(x == farest)
                            {
                                delay[y] = d;
                                ahp_xc_set_channel_auto(y, 0, 1, 1);
                                ahp_xc_set_channel_cross(y, delay_clocks, 1, 1);
                            }
                        }
                        idx++;
                    }
                }
                // Filling BLOBs
                if(nplots > 0)
                {
                    idx = 0;
                    for(unsigned int x = 0; x < ahp_xc_get_nlines(); x++)
                    {
                        for(unsigned int y = x + 1; y < ahp_xc_get_nlines(); y++)
                        {
                            if((lineEnableSP[x].sp[0].s == ISS_ON) && lineEnableSP[y].sp[0].s == ISS_ON)
                            {
                                int w = plot_str[0]->sizes[0];
                                int h = plot_str[0]->sizes[1];
                                INDI::Correlator::UVCoordinate uv = baselines[idx]->getUVCoordinates(Altitude, Azimuth);
                                int xx = static_cast<int>(w * uv.u / 2.0);
                                int yy = static_cast<int>(h * uv.v / 2.0);
                                int z = w * h / 2 + w / 2 + xx + yy * w;
                                if(xx >= -w / 2 && xx < w / 2 && yy >= -w / 2 && yy < h / 2)
                                {
                                    plot_str[0]->buf[z] += (double)packet->crosscorrelations[idx].correlations[packet->crosscorrelations[idx].lag_size /
                                                           2].magnitude / (double)packet->crosscorrelations[idx].correlations[packet->crosscorrelations[idx].lag_size /
                                                                   2].counts;
                                    plot_str[0]->buf[w * h - 1 - z] += (double)
                                                                       packet->crosscorrelations[idx].correlations[packet->crosscorrelations[idx].lag_size / 2].magnitude /
                                                                       packet->crosscorrelations[idx].correlations[packet->crosscorrelations[idx].lag_size / 2].counts;
                                }
                            }
                            idx++;
                        }
                    }
                }
                if(ahp_xc_get_nlines() > 0 && ahp_xc_get_autocorrelator_lagsize() > 1)
                {
                    for(unsigned int x = 0; x < ahp_xc_get_nlines(); x++)
                    {
                        int pos = autocorrelations_str[x]->len - autocorrelations_str[x]->sizes[0];
                        autocorrelations_str[x]->sizes[1]++;
                        autocorrelations_str[x]->len += autocorrelations_str[x]->sizes[0];
                        autocorrelations_str[x]->buf = (dsp_t*)realloc(autocorrelations_str[x]->buf,
                                                       sizeof(dsp_t) * static_cast<unsigned int>(autocorrelations_str[x]->len));
                        for(unsigned int i = 0; i < packet->autocorrelations[x].lag_size; i++)
                            autocorrelations_str[x]->buf[pos++] = packet->autocorrelations[x].correlations[i].magnitude;
                    }
                }
                if(ahp_xc_get_nbaselines() > 0 && ahp_xc_get_crosscorrelator_lagsize() > 1)
                {
                    for(unsigned int x = 0; x < ahp_xc_get_nbaselines(); x++)
                    {
                        int pos = crosscorrelations_str[x]->len - crosscorrelations_str[x]->sizes[0];
                        crosscorrelations_str[x]->sizes[1]++;
                        crosscorrelations_str[x]->len += crosscorrelations_str[x]->sizes[0];
                        crosscorrelations_str[x]->buf = (dsp_t*)realloc(crosscorrelations_str[x]->buf,
                                                        sizeof(dsp_t) * static_cast<unsigned int>(crosscorrelations_str[x]->len));
                        for(unsigned int i = 0; i < packet->crosscorrelations[x].lag_size; i++)
                            crosscorrelations_str[x]->buf[pos++] = packet->crosscorrelations[x].correlations[i].magnitude;
                    }
                }
            }
        }

        idx = 0;
        for(unsigned int x = 0; x < ahp_xc_get_nlines(); x++)
        {
            if(lineEnableSP[x].sp[0].s == ISS_ON)
                totalcounts[x] += packet->counts[x];
            for(unsigned int y = x + 1; y < ahp_xc_get_nlines(); y++)
            {
                if((lineEnableSP[x].sp[0].s == ISS_ON) && lineEnableSP[y].sp[0].s == ISS_ON)
                {
                    totalcorrelations[idx].counts += packet->crosscorrelations[idx].correlations[packet->crosscorrelations[idx].lag_size /
                                                     2].counts;
                    totalcorrelations[idx].magnitude += packet->crosscorrelations[idx].correlations[packet->crosscorrelations[idx].lag_size /
                                                        2].magnitude;
                }
                idx++;
            }
        }
        ahp_xc_free_packet(packet);
    }
}

AHP_XC::AHP_XC()
{
    clock_divider = 0;

    IntegrationRequest = 0.0;
    InIntegration = false;

    autocorrelationsB = static_cast<IBLOB*>(malloc(1));
    crosscorrelationsB = static_cast<IBLOB*>(malloc(1));
    plotB = static_cast<IBLOB*>(malloc(1));

    lineStatsN = static_cast<INumber*>(malloc(1));
    lineStatsNP = static_cast<INumberVectorProperty*>(malloc(1));

    lineEnableS = static_cast<ISwitch*>(malloc(1));
    lineEnableSP = static_cast<ISwitchVectorProperty*>(malloc(1));

    linePowerS = static_cast<ISwitch*>(malloc(1));
    linePowerSP = static_cast<ISwitchVectorProperty*>(malloc(1));

    lineActiveEdgeS = static_cast<ISwitch*>(malloc(1));
    lineActiveEdgeSP = static_cast<ISwitchVectorProperty*>(malloc(1));

    lineEdgeTriggerS = static_cast<ISwitch*>(malloc(1));
    lineEdgeTriggerSP = static_cast<ISwitchVectorProperty*>(malloc(1));

    lineDifferentialS = static_cast<ISwitch*>(malloc(1));
    lineDifferentialSP = static_cast<ISwitchVectorProperty*>(malloc(1));

    lineLocationN = static_cast<INumber*>(malloc(1));
    lineLocationNP = static_cast<INumberVectorProperty*>(malloc(1));

    lineDelayN = static_cast<INumber*>(malloc(1));
    lineDelayNP = static_cast<INumberVectorProperty*>(malloc(1));

    correlationsN = static_cast<INumber*>(malloc(1));

    autocorrelations_str = static_cast<dsp_stream_p*>(malloc(1));
    crosscorrelations_str = static_cast<dsp_stream_p*>(malloc(1));
    plot_str = static_cast<dsp_stream_p*>(malloc(1));

    framebuffer = static_cast<double*>(malloc(1));
    totalcounts = static_cast<double*>(malloc(1));
    totalcorrelations = static_cast<ahp_xc_correlation*>(malloc(1));
    delay = static_cast<double*>(malloc(1));
    baselines = static_cast<baseline**>(malloc(1));

    setSensorConnection(CONNECTION_SERIAL);

    SetSpectrographCapability(SENSOR_CAN_ABORT | SENSOR_HAS_DSP);

    // Start the timer
    SetTimer(getCurrentPollingPeriod());

    readThread = std::thread(&AHP_XC::Callback, this);
}

bool AHP_XC::Disconnect()
{
    if(!isConnected()) return false;

    EnableCapture(false);

    if(ahp_xc_get_autocorrelator_lagsize() > 1)
        deleteProperty(autocorrelationsBP.name);
    if(ahp_xc_get_crosscorrelator_lagsize() > 1)
        deleteProperty(crosscorrelationsBP.name);
    deleteProperty(correlationsNP.name);
    deleteProperty(settingsNP.name);
    for(unsigned int x = 0; x < ahp_xc_get_nlines(); x++) {
        deleteProperty(lineStatsNP[x].name);
        deleteProperty(lineEnableSP[x].name);
        deleteProperty(linePowerSP[x].name);
        deleteProperty(lineActiveEdgeSP[x].name);
        deleteProperty(lineEdgeTriggerSP[x].name);
        deleteProperty(lineDifferentialSP[x].name);
        deleteProperty(lineLocationNP[x].name);
        deleteProperty(lineDelayNP[x].name);
    }
    for(unsigned int x = 0; x < nplots; x++)
    {
        dsp_stream_free_buffer(plot_str[x]);
        dsp_stream_free(plot_str[x]);
    }
    for(unsigned int x = 0; x < ahp_xc_get_nlines(); x++)
    {
        if(ahp_xc_get_autocorrelator_lagsize() > 1)
        {
            dsp_stream_free_buffer(autocorrelations_str[x]);
            dsp_stream_free(autocorrelations_str[x]);
        }
        ActiveLine(x, false, false, false, false, false);
        usleep(10000);
    }
    for(unsigned int x = 0; x < ahp_xc_get_nbaselines(); x++)
    {
        if(ahp_xc_get_crosscorrelator_lagsize() > 1)
        {
            dsp_stream_free_buffer(crosscorrelations_str[x]);
            dsp_stream_free(crosscorrelations_str[x]);
        }
    }

    ahp_xc_disconnect();

    return true;
}

const char * AHP_XC::getDefaultName()
{
    return "AHP XC Correlator";
}

const char * AHP_XC::getDeviceName()
{
    return getDefaultName();
}

bool AHP_XC::saveConfigItems(FILE *fp)
{
    for(unsigned int x = 0; x < ahp_xc_get_nlines(); x++)
    {
        IUSaveConfigSwitch(fp, &lineEnableSP[x]);
        if(lineEnableSP[x].sp[0].s == ISS_ON)
        {
            IUSaveConfigSwitch(fp, &linePowerSP[x]);
            IUSaveConfigSwitch(fp, &lineActiveEdgeSP[x]);
            IUSaveConfigSwitch(fp, &lineEdgeTriggerSP[x]);
            IUSaveConfigSwitch(fp, &lineDifferentialSP[x]);
            IUSaveConfigNumber(fp, &lineLocationNP[x]);
        }
    }
    IUSaveConfigNumber(fp, &settingsNP);

    INDI::Spectrograph::saveConfigItems(fp);
    return true;
}

/**************************************************************************************
** INDI is asking us to init our properties.
***************************************************************************************/
bool AHP_XC::initProperties()
{

    // Must init parent properties first!
    INDI::Spectrograph::initProperties();

    // Set minimum exposure speed to 0.001 seconds
    setMinMaxStep("SENSOR_INTEGRATION", "SENSOR_INTEGRATION_VALUE", 1.0, STELLAR_DAY, 1, true);
    setMinMaxStep("SPECTROGRAPH_SETTINGS", "SPECTROGRAPH_GAIN", 0.0, 0.0, 1, true);
    setMinMaxStep("SPECTROGRAPH_SETTINGS", "SPECTROGRAPH_LOWFREQ", 1.0e+3, 1.0e+8, 1, true);
    setMinMaxStep("SPECTROGRAPH_SETTINGS", "SPECTROGRAPH_HIGHFREQ", 1.0e+3, 1.0e+8, 1, true);
    setMinMaxStep("SPECTROGRAPH_SETTINGS", "SPECTROGRAPH_BITSPERSAMPLE", 16, 16, 1, true);
    setMinMaxStep("SPECTROGRAPH_SETTINGS", "SPECTROGRAPH_ANTENNA", 1, 1, 1, true);

    IUFillNumber(&settingsN[0], "CROSSCORRELATOR_WAVELENGTH_VALUE", "Filter wavelength (m)", "%g", 3.0E-12, 3.0E+3, 1.0E-9,
                 0.211121449);
    IUFillNumber(&settingsN[1], "CROSSCORRELATOR_BANDWIDTH_VALUE", "Filter bandwidth (m)", "%g", 3.0E-12, 3.0E+3, 1.0E-9,
                 1199.169832);
    IUFillNumber(&settingsN[2], "CROSSCORRELATOR_PLOT_SIZE", "Plot size (px)", "%g", 128, 8192, 64,
                 512);
    IUFillNumberVector(&settingsNP, settingsN, 3, getDeviceName(), "CROSSCORRELATOR_SETTINGS", "AHP_XC Settings",
                       MAIN_CONTROL_TAB, IP_RW, 60, IPS_IDLE);

    setIntegrationFileExtension("fits");
    setDefaultPollingPeriod(500);

    getSerialConnection()->setDefaultBaudRate(Connection::Serial::B_57600);

    return true;
}

/**************************************************************************************
** INDI is asking us to submit list of properties for the device
***************************************************************************************/
void AHP_XC::ISGetProperties(const char *dev)
{
    INDI::Spectrograph::ISGetProperties(dev);
}

/********************************************************************************************
** INDI is asking us to update the properties because there is a change in CONNECTION status
** This fucntion is called whenever the device is connected or disconnected.
*********************************************************************************************/
bool AHP_XC::updateProperties()
{
    // Call parent update properties
    INDI::Spectrograph::updateProperties();

    if(isConnected()) {
        if(ahp_xc_get_autocorrelator_lagsize() > 1)
            defineProperty(&autocorrelationsBP);
        if(ahp_xc_get_crosscorrelator_lagsize() > 1)
            defineProperty(&crosscorrelationsBP);
        defineProperty(&correlationsNP);
        defineProperty(&settingsNP);
        for(unsigned int x = 0; x < ahp_xc_get_nlines(); x++) {
            defineProperty(&lineEnableSP[x]);
        }
    } else {
        if(ahp_xc_get_autocorrelator_lagsize() > 1)
            deleteProperty(autocorrelationsBP.name);
        if(ahp_xc_get_crosscorrelator_lagsize() > 1)
            deleteProperty(crosscorrelationsBP.name);
        deleteProperty(correlationsNP.name);
        deleteProperty(settingsNP.name);
        for(unsigned int x = 0; x < ahp_xc_get_nlines(); x++) {
            deleteProperty(lineEnableSP[x].name);
        }
    }

    for(unsigned int x = 0; x < ahp_xc_get_nbaselines(); x++)
        baselines[x]->updateProperties();

    return true;
}

/**************************************************************************************
** Setting up CCD parameters
***************************************************************************************/
void AHP_XC::setupParams()
{
    int size = (float)settingsN[2].value;

    if(nplots > 0)
    {
        for(unsigned int x = 0; x < nplots; x++) {
            dsp_stream_set_dim(plot_str[x], 0, size);
            dsp_stream_set_dim(plot_str[x], 1, size);
            dsp_stream_alloc_buffer(plot_str[x], plot_str[x]->len);
        }
    }
}

/**************************************************************************************
** Client is asking us to start an exposure
***************************************************************************************/
bool AHP_XC::StartIntegration(double duration)
{
    if(InIntegration)
        return false;

    IntegrationRequest = static_cast<double>(duration);
    gettimeofday(&ExpStart, nullptr);
    InIntegration = true;
    // We're done
    return true;
}

/**************************************************************************************
** Client is asking us to abort an exposure
***************************************************************************************/
bool AHP_XC::AbortIntegration()
{
    InIntegration = false;
    return true;
}

/**************************************************************************************
** Client is asking us to set a new number
***************************************************************************************/
bool AHP_XC::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    INDI::Spectrograph::ISNewNumber(dev, name, values, names, n);

    if (strcmp (dev, getDeviceName()))
        return false;

    for(unsigned int x = 0; x < ahp_xc_get_nbaselines(); x++)
        baselines[x]->ISNewNumber(dev, name, values, names, n);

    for(unsigned int i = 0; i < ahp_xc_get_nlines(); i++)
    {
        if(!strcmp(lineLocationNP[i].name, name))
        {
            IUUpdateNumber(&lineLocationNP[i], values, names, n);
            int idx = 0;
            for(unsigned int x = 0; x < ahp_xc_get_nlines(); x++)
            {
                for(unsigned int y = x + 1; y < ahp_xc_get_nlines(); y++)
                {
                    if(x == i || y == i)
                    {
                        INDI::Correlator::Baseline b;
                        b.x = lineLocationNP[y].np[0].value - lineLocationNP[x].np[0].value;
                        b.y = lineLocationNP[y].np[1].value - lineLocationNP[x].np[1].value;
                        b.z = lineLocationNP[y].np[2].value - lineLocationNP[x].np[2].value;
                        baselines[idx]->setBaseline(b);
                    }
                    idx++;
                }
            }
            IDSetNumber(&lineLocationNP[i], nullptr);
        }
    }

    if(!strcmp(settingsNP.name, name))
    {
        double center_freq = LIGHTSPEED/settingsN[0].value;
        double bandwidth_freq = LIGHTSPEED/settingsN[1].value;
        double lofreq = center_freq - bandwidth_freq / 2;
        double hifreq = center_freq + bandwidth_freq / 2;
        SpectrographSettingsN[SPECTROGRAPH_LOWFREQ].value = lofreq;
        SpectrographSettingsN[SPECTROGRAPH_HIGHFREQ].value = hifreq;
        IUUpdateNumber(&settingsNP, values, names, n);
        for(unsigned int x = 0; x < ahp_xc_get_nbaselines(); x++)
        {
            baselines[x]->setWavelength(settingsN[0].value);
        }
        IDSetNumber(&SpectrographSettingsNP, nullptr);
        setupParams();
        return true;
    }

    if(!strcmp(SpectrographSettingsNP.name, name))
    {
        IUUpdateNumber(&SpectrographSettingsNP, values, names, n);
        double bandwidth_freq = SpectrographSettingsN[SPECTROGRAPH_HIGHFREQ].value - SpectrographSettingsN[SPECTROGRAPH_LOWFREQ].value;
        double center_freq = SpectrographSettingsN[SPECTROGRAPH_LOWFREQ].value + bandwidth_freq / 2.0;
        settingsN[0].value = LIGHTSPEED/center_freq;
        settingsN[1].value = LIGHTSPEED/bandwidth_freq;
        IUUpdateNumber(&SpectrographSettingsNP, values, names, n);
        IDSetNumber(&settingsNP, nullptr);
        return true;
    }

    return true;
}

/**************************************************************************************
** Client is asking us to set a new switch
***************************************************************************************/
bool AHP_XC::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    INDI::Spectrograph::ISNewSwitch(dev, name, states, names, n);

    if (strcmp (dev, getDeviceName()))
        return false;

    for(unsigned int x = 0; x < ahp_xc_get_nbaselines(); x++)
        baselines[x]->ISNewSwitch(dev, name, states, names, n);

    for(unsigned int x = 0; x < ahp_xc_get_nlines(); x++)
    {
        if(!strcmp(name, lineEnableSP[x].name))
        {
            IUUpdateSwitch(&lineEnableSP[x], states, names, n);
            if(lineEnableSP[x].sp[0].s == ISS_ON)
            {
                ActiveLine(x, lineEnableSP[x].sp[0].s == ISS_ON
                           || ahp_xc_has_leds(), linePowerSP[x].sp[0].s == ISS_ON, lineActiveEdgeSP[x].sp[1].s == ISS_ON,
                           lineEdgeTriggerSP[x].sp[1].s == ISS_ON, lineDifferentialSP[x].sp[1].s == ISS_ON);
                defineProperty(&linePowerSP[x]);
                defineProperty(&lineActiveEdgeSP[x]);
                defineProperty(&lineEdgeTriggerSP[x]);
                defineProperty(&lineDifferentialSP[x]);
                defineProperty(&lineLocationNP[x]);
                defineProperty(&lineStatsNP[x]);
                defineProperty(&lineDelayNP[x]);
            }
            else
            {
                ActiveLine(x, false, false, false, false, false);
                deleteProperty(linePowerSP[x].name);
                deleteProperty(lineActiveEdgeSP[x].name);
                deleteProperty(lineEdgeTriggerSP[x].name);
                deleteProperty(lineDifferentialSP[x].name);
                deleteProperty(lineLocationNP[x].name);
                deleteProperty(lineStatsNP[x].name);
                deleteProperty(lineDelayNP[x].name);
            }
            IDSetSwitch(&lineEnableSP[x], nullptr);
        }
        if(!strcmp(name, linePowerSP[x].name))
        {
            IUUpdateSwitch(&linePowerSP[x], states, names, n);
            ActiveLine(x, lineEnableSP[x].sp[0].s == ISS_ON
                       || ahp_xc_has_leds(), linePowerSP[x].sp[0].s == ISS_ON, lineActiveEdgeSP[x].sp[1].s == ISS_ON,
                    lineEdgeTriggerSP[x].sp[1].s == ISS_ON,
                    lineDifferentialSP[x].sp[0].s == ISS_ON);
            IDSetSwitch(&linePowerSP[x], nullptr);
        }
        if(!strcmp(name, lineActiveEdgeSP[x].name))
        {
            IUUpdateSwitch(&lineActiveEdgeSP[x], states, names, n);
            ActiveLine(x, lineEnableSP[x].sp[0].s == ISS_ON
                       || ahp_xc_has_leds(), linePowerSP[x].sp[0].s == ISS_ON, lineActiveEdgeSP[x].sp[1].s == ISS_ON,
                    lineEdgeTriggerSP[x].sp[1].s == ISS_ON,
                    lineDifferentialSP[x].sp[0].s == ISS_ON);
            IDSetSwitch(&lineActiveEdgeSP[x], nullptr);
        }
        if(!strcmp(name, lineEdgeTriggerSP[x].name))
        {
            IUUpdateSwitch(&lineEdgeTriggerSP[x], states, names, n);
            ActiveLine(x, lineEnableSP[x].sp[0].s == ISS_ON
                       || ahp_xc_has_leds(), linePowerSP[x].sp[0].s == ISS_ON, lineActiveEdgeSP[x].sp[1].s == ISS_ON,
                    lineEdgeTriggerSP[x].sp[1].s == ISS_ON,
                    lineDifferentialSP[x].sp[0].s == ISS_ON);
            IDSetSwitch(&lineEdgeTriggerSP[x], nullptr);
        }
        if(!strcmp(name, lineDifferentialSP[x].name))
        {
            IUUpdateSwitch(&lineDifferentialSP[x], states, names, n);
            ActiveLine(x, lineEnableSP[x].sp[0].s == ISS_ON
                       || ahp_xc_has_leds(), linePowerSP[x].sp[0].s == ISS_ON, lineActiveEdgeSP[x].sp[1].s == ISS_ON,
                    lineEdgeTriggerSP[x].sp[1].s == ISS_ON,
                    lineDifferentialSP[x].sp[0].s == ISS_ON);
            IDSetSwitch(&lineDifferentialSP[x], nullptr);
        }
    }

    return true;
}

/**************************************************************************************
** Client is asking us to set a new BLOB
***************************************************************************************/
bool AHP_XC::ISNewBLOB(const char *dev, const char *name, int sizes[], int blobsizes[], char *blobs[], char *formats[],
                       char *names[], int n)
{
    INDI::Spectrograph::ISNewBLOB(dev, name, sizes, blobsizes, blobs, formats, names, n);

    if (strcmp (dev, getDeviceName()))
        return false;

    for(unsigned int x = 0; x < ahp_xc_get_nbaselines(); x++)
        baselines[x]->ISNewBLOB(dev, name, sizes, blobsizes, blobs, formats, names, n);

    return true;
}

/**************************************************************************************
** Client is asking us to set a new text
***************************************************************************************/
bool AHP_XC::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    INDI::Spectrograph::ISNewText(dev, name, texts, names, n);

    if (strcmp (dev, getDeviceName()))
        return false;

    for(unsigned int x = 0; x < ahp_xc_get_nbaselines(); x++)
        baselines[x]->ISNewText(dev, name, texts, names, n);

    return true;
}

/**************************************************************************************
** Client is asking us to set a new snoop device
***************************************************************************************/
bool AHP_XC::ISSnoopDevice(XMLEle *root)
{
    for(unsigned int x = 0; x < ahp_xc_get_nbaselines(); x++)
        baselines[x]->ISSnoopDevice(root);

    INDI::Spectrograph::ISSnoopDevice(root);

    return true;
}

/**************************************************************************************
** INDI is asking us to add any FITS keywords to the FITS header
***************************************************************************************/
void AHP_XC::addFITSKeywords(fitsfile *fptr, uint8_t* buf, int len)
{
    // Let's first add parent keywords
    INDI::Spectrograph::addFITSKeywords(fptr, buf, len);

    // Add temperature to FITS header
    int status = 0;
    fits_write_date(fptr, &status);

}

double AHP_XC::CalcTimeLeft()
{
    double timesince;
    double timeleft;
    struct timeval now;
    gettimeofday(&now, nullptr);

    timesince = (double)(now.tv_sec * 1000.0 + now.tv_usec / 1000) - (double)(ExpStart.tv_sec * 1000.0 + ExpStart.tv_usec /
                1000);
    timesince = timesince / 1000.0;

    timeleft = IntegrationRequest - timesince;
    return timeleft;
}

/**************************************************************************************
** Main device loop. We check for exposure and temperature progress here
***************************************************************************************/
void AHP_XC::TimerHit()
{
    if(!isConnected())
        return;  //  No need to reset timer if we are not connected anymore

    int idx = 0;
    correlationsNP.s = IPS_BUSY;
    for (unsigned int x = 0; x < ahp_xc_get_nlines(); x++)
    {
        double line_delay = delay[x];
        double steradian = pow(asin(primaryAperture * 0.5 / primaryFocalLength), 2);
        double photon_flux = ((double)totalcounts[x]) * 1000.0 / getCurrentPollingPeriod();
        double photon_flux0 = calc_photon_flux(0, settingsNP.np[1].value, settingsNP.np[0].value, steradian);
        lineDelayNP[x].s = IPS_BUSY;
        lineDelayNP[x].np[0].value = line_delay;
        IDSetNumber(&lineDelayNP[x], nullptr);
        lineStatsNP[x].s = IPS_BUSY;
        lineStatsNP[x].np[0].value = ((double)totalcounts[x]) * 1000.0 / (double)getCurrentPollingPeriod();
        lineStatsNP[x].np[1].value = photon_flux / LUMEN(settingsNP.np[0].value);
        lineStatsNP[x].np[2].value = photon_flux0 / LUMEN(settingsNP.np[0].value);
        lineStatsNP[x].np[3].value = calc_rel_magnitude(photon_flux, settingsNP.np[1].value, settingsNP.np[0].value, steradian);
        IDSetNumber(&lineStatsNP[x], nullptr);
        totalcounts[x] = 0;
        for(unsigned int y = x + 1; y < ahp_xc_get_nlines(); y++)
        {
            correlationsNP.np[idx * 2].value = (double)totalcorrelations[idx].magnitude * 1000.0 / (double)getCurrentPollingPeriod();
            correlationsNP.np[idx * 2 + 1].value = (double)totalcorrelations[idx].magnitude / (double)totalcorrelations[idx].counts;
            totalcorrelations[idx].counts = 0;
            totalcorrelations[idx].magnitude = 0;
            totalcorrelations[idx].phase = 0;
            totalcorrelations[idx].counts = 0;
            idx++;
        }
    }
    IDSetNumber(&correlationsNP, nullptr);

    if(InIntegration)
    {
        setIntegrationLeft((double)timeleft);
    }

    SetTimer(getCurrentPollingPeriod());

    return;
}

bool AHP_XC::Handshake()
{
    if(0 != ahp_xc_connect_fd(getPortFD()))
    {
        ahp_xc_disconnect();
        return false;
    }

    if(!ahp_xc_is_connected())
    {
        return false;
    }

    if(0 != ahp_xc_get_properties())
    {
        ahp_xc_disconnect();
        return false;
    }

    if(!ahp_xc_is_detected())
    {
        ahp_xc_disconnect();
        return false;
    }

    lineStatsN = static_cast<INumber*>(realloc(lineStatsN,
                                       static_cast<unsigned long>(4 * ahp_xc_get_nlines()) * sizeof(INumber) + 1));
    lineStatsNP = static_cast<INumberVectorProperty*>(realloc(lineStatsNP,
                  static_cast<unsigned long>(ahp_xc_get_nlines()) * sizeof(INumberVectorProperty) + 1));

    lineEnableS = static_cast<ISwitch*>(realloc(lineEnableS,
                                        static_cast<unsigned long>(ahp_xc_get_nlines()) * 2 * sizeof(ISwitch)));
    lineEnableSP = static_cast<ISwitchVectorProperty*>(realloc(lineEnableSP,
                   static_cast<unsigned long>(ahp_xc_get_nlines()) * sizeof(ISwitchVectorProperty) + 1));

    linePowerS = static_cast<ISwitch*>(realloc(linePowerS,
                                       static_cast<unsigned long>(ahp_xc_get_nlines()) * 2 * sizeof(ISwitch) + 1));
    linePowerSP = static_cast<ISwitchVectorProperty*>(realloc(linePowerSP,
                  static_cast<unsigned long>(ahp_xc_get_nlines()) * sizeof(ISwitchVectorProperty) + 1));

    lineActiveEdgeS = static_cast<ISwitch*>(realloc(lineActiveEdgeS,
                                            static_cast<unsigned long>(ahp_xc_get_nlines()) * 2 * sizeof(ISwitch) + 1));
    lineActiveEdgeSP = static_cast<ISwitchVectorProperty*>(realloc(lineActiveEdgeSP,
                       static_cast<unsigned long>(ahp_xc_get_nlines()) * sizeof(ISwitchVectorProperty) + 1));

    lineEdgeTriggerS = static_cast<ISwitch*>(realloc(lineEdgeTriggerS,
                       static_cast<unsigned long>(ahp_xc_get_nlines()) * 2 * sizeof(ISwitch) + 1));
    lineEdgeTriggerSP = static_cast<ISwitchVectorProperty*>(realloc(lineEdgeTriggerSP,
                        static_cast<unsigned long>(ahp_xc_get_nlines()) * sizeof(ISwitchVectorProperty) + 1));

    lineDifferentialS = static_cast<ISwitch*>(realloc(lineDifferentialS,
                       static_cast<unsigned long>(ahp_xc_get_nlines()) * 2 * sizeof(ISwitch) + 1));
    lineDifferentialSP = static_cast<ISwitchVectorProperty*>(realloc(lineDifferentialSP,
                        static_cast<unsigned long>(ahp_xc_get_nlines()) * sizeof(ISwitchVectorProperty) + 1));

    lineLocationN = static_cast<INumber*>(realloc(lineLocationN,
                                          static_cast<unsigned long>(ahp_xc_get_nlines() * 3) * sizeof(INumber) + 1));
    lineLocationNP = static_cast<INumberVectorProperty*>(realloc(lineLocationNP,
                     static_cast<unsigned long>(ahp_xc_get_nlines()) * sizeof(INumberVectorProperty) + 1));

    lineDelayN = static_cast<INumber*>(realloc(lineDelayN,
                                       static_cast<unsigned long>(ahp_xc_get_nlines()) * sizeof(INumber) + 1));
    lineDelayNP = static_cast<INumberVectorProperty*>(realloc(lineDelayNP,
                  static_cast<unsigned long>(ahp_xc_get_nlines()) * sizeof(INumberVectorProperty) + 1));

    correlationsN = static_cast<INumber*>(realloc(correlationsN,
                                          static_cast<unsigned long>(ahp_xc_get_nbaselines() * 2) * sizeof(INumber) + 1));

    if(ahp_xc_get_autocorrelator_lagsize() > 1)
        autocorrelationsB = static_cast<IBLOB*>(realloc(autocorrelationsB,
                                                static_cast<unsigned long>(ahp_xc_get_nlines()) * sizeof(IBLOB) + 1));
    if(ahp_xc_get_crosscorrelator_lagsize() > 1)
        crosscorrelationsB = static_cast<IBLOB*>(realloc(crosscorrelationsB,
                             static_cast<unsigned long>(ahp_xc_get_nbaselines()) * sizeof(IBLOB) + 1));
    if(nplots > 0)
        plotB = static_cast<IBLOB*>(realloc(plotB, static_cast<unsigned long>(nplots) * sizeof(IBLOB) + 1));

    if(ahp_xc_get_autocorrelator_lagsize() > 1)
        autocorrelations_str = static_cast<dsp_stream_p*>(realloc(autocorrelations_str,
                               static_cast<unsigned long>(ahp_xc_get_nlines()) * sizeof(dsp_stream_p) + 1));
    if(ahp_xc_get_crosscorrelator_lagsize() > 1)
        crosscorrelations_str = static_cast<dsp_stream_p*>(realloc(crosscorrelations_str,
                                static_cast<unsigned long>(ahp_xc_get_nbaselines()) * sizeof(dsp_stream_p) + 1));
    if(nplots > 0)
        plot_str = static_cast<dsp_stream_p*>(realloc(plot_str, static_cast<unsigned long>(nplots) * sizeof(dsp_stream_p) + 1));

    totalcounts = static_cast<double*>(realloc(totalcounts,
                                       static_cast<unsigned long>(ahp_xc_get_nlines()) * sizeof(double) +1));
    totalcorrelations = static_cast<ahp_xc_correlation*>(realloc(totalcorrelations,
                        static_cast<unsigned long>(ahp_xc_get_nbaselines()) * sizeof(ahp_xc_correlation) + 1));
    delay = static_cast<double*>(realloc(delay, static_cast<unsigned long>(ahp_xc_get_nlines()) * sizeof(double) +1));
    baselines = static_cast<baseline**>(realloc(baselines,
                                        static_cast<unsigned long>(ahp_xc_get_nbaselines()) * sizeof(baseline*) + 1));
    center = static_cast<INDI::Correlator::Baseline*>(malloc(sizeof (INDI::Correlator::Baseline) * static_cast<unsigned long>
             (ahp_xc_get_nlines())));

    memset (totalcounts, 0, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(double) +1);
    memset (totalcorrelations, 0, static_cast<unsigned long>(ahp_xc_get_nbaselines())*sizeof(ahp_xc_correlation) + 1);
    for(unsigned int x = 0; x < ahp_xc_get_nbaselines(); x++)
    {
        if(ahp_xc_get_crosscorrelator_lagsize() > 1)
        {
            crosscorrelations_str[x] = dsp_stream_new();
            dsp_stream_add_dim(crosscorrelations_str[x], static_cast<int>(ahp_xc_get_crosscorrelator_lagsize() * 2 - 1));
            dsp_stream_add_dim(crosscorrelations_str[x], 1);
            dsp_stream_alloc_buffer(crosscorrelations_str[x], crosscorrelations_str[x]->len);
        }
        baselines[x] = new baseline();
        baselines[x]->initProperties();
    }

    int idx = 0;
    char tab[MAXINDINAME];
    char name[MAXINDINAME];
    char label[MAXINDINAME];

    for(unsigned int x = 0; x < nplots; x++)
    {
        plot_str[x] = dsp_stream_new();
        dsp_stream_add_dim(plot_str[x], 1);
        dsp_stream_add_dim(plot_str[x], 1);
        dsp_stream_alloc_buffer(plot_str[x], plot_str[x]->len);
        sprintf(name, "PLOT%02d", x + 1);
        char prefix[50] = { 0 };
        if(nplots > 1)
            sprintf(prefix, "_%03d", x + 1);
        sprintf(label, "Plot%s", prefix);
        IUFillBLOB(&plotB[x], name, label, ".fits");
    }
    IUFillBLOBVector(&plotBP, plotB, static_cast<int>(nplots), getDeviceName(), "PLOTS", "Plots", "Stats", IP_RO, 60, IPS_BUSY);

    for (unsigned int x = 0; x < ahp_xc_get_nlines(); x++)
    {
        if(ahp_xc_get_autocorrelator_lagsize() > 1)
        {
            autocorrelations_str[x] = dsp_stream_new();
            dsp_stream_add_dim(autocorrelations_str[x], static_cast<int>(ahp_xc_get_autocorrelator_lagsize()));
            dsp_stream_add_dim(autocorrelations_str[x], 1);
            dsp_stream_alloc_buffer(autocorrelations_str[x], autocorrelations_str[x]->len);
        }

        IUFillNumber(&lineLocationN[x * 3 + 0], "LOCATION_X", "X Location (m)", "%g", -EARTHRADIUSMEAN, EARTHRADIUSMEAN, 1.0E-9, 0);
        IUFillNumber(&lineLocationN[x * 3 + 1], "LOCATION_Y", "Y Location (m)", "%g", -EARTHRADIUSMEAN, EARTHRADIUSMEAN, 1.0E-9, 0);
        IUFillNumber(&lineLocationN[x * 3 + 2], "LOCATION_Z", "Z Location (m)", "%g", -EARTHRADIUSMEAN, EARTHRADIUSMEAN, 1.0E-9, 0);

        IUFillNumber(&lineDelayN[x], "DELAY", "Delay (m)", "%g", 0, EARTHRADIUSMEAN, 1.0E-9, 0);

        //interferometer properties
        IUFillSwitch(&lineEnableS[x * 2 + 0], "LINE_ENABLE", "Enable", ISS_OFF);
        IUFillSwitch(&lineEnableS[x * 2 + 1], "LINE_DISABLE", "Disable", ISS_ON);

        IUFillSwitch(&linePowerS[x * 2 + 0], "LINE_POWER_ON", "On", ISS_OFF);
        IUFillSwitch(&linePowerS[x * 2 + 1], "LINE_POWER_OFF", "Off", ISS_ON);

        IUFillSwitch(&lineActiveEdgeS[x * 2 + 0], "LINE_ACTIVE_EDGE_HIGH", "High", ISS_ON);
        IUFillSwitch(&lineActiveEdgeS[x * 2 + 1], "LINE_ACTIVE_EDGE_LOW", "Low", ISS_OFF);

        IUFillSwitch(&lineEdgeTriggerS[x * 2 + 0], "LINE_EDGE_SAMPLE", "On sample", ISS_OFF);
        IUFillSwitch(&lineEdgeTriggerS[x * 2 + 1], "LINE_EDGE_EDGE", "On edge", ISS_ON);

        IUFillSwitch(&lineDifferentialS[x * 2 + 0], "LINE_DIFFERENTIAL_ON", "Differential sampling", ISS_OFF);
        IUFillSwitch(&lineDifferentialS[x * 2 + 1], "LINE_DIFFERENTIAL_OFF", "Multiplicative sampling", ISS_ON);

        //report pulse counts
        IUFillNumber(&lineStatsN[x * 4 + 0], "LINE_COUNTS", "Counts", "%g", 0.0, 400000000.0, 1.0, 0);
        IUFillNumber(&lineStatsN[x * 4 + 1], "LINE_FLUX", "Photon Flux (Lm)", "%g", 0.0, 1.0, 1.0E-5, 0);
        IUFillNumber(&lineStatsN[x * 4 + 2], "LINE_FLUX0", "Flux at mag0 (Lm)", "%g", 0.0, 1.0, 1.0E-5, 0);
        IUFillNumber(&lineStatsN[x * 4 + 3], "LINE_MAGNITUDE", "Estimated magnitude", "%g", -22.0, 22.0, 1.0E-5, 0);

        sprintf(tab, "Line %02d", x + 1);
        sprintf(name, "LINE_ENABLE_%02d", x + 1);
        IUFillSwitchVector(&lineEnableSP[x], &lineEnableS[x * 2], 2, getDeviceName(), name, "Enable Line", tab, IP_RW, ISR_1OFMANY,
                           60, IPS_IDLE);
        sprintf(name, "LINE_POWER_%02d", x + 1);
        IUFillSwitchVector(&linePowerSP[x], &linePowerS[x * 2], 2, getDeviceName(), name, "Power", tab, IP_RW, ISR_1OFMANY, 60,
                           IPS_IDLE);
        sprintf(name, "LINE_ACTIVE_EDGE_%02d", x + 1);
        IUFillSwitchVector(&lineActiveEdgeSP[x], &lineActiveEdgeS[x * 2], 2, getDeviceName(), name, "Active edge", tab, IP_RW,
                           ISR_1OFMANY, 60, IPS_IDLE);
        sprintf(name, "LINE_EDGE_TRIGGER_%02d", x + 1);
        IUFillSwitchVector(&lineEdgeTriggerSP[x], &lineEdgeTriggerS[x * 2], 2, getDeviceName(), name, "Trigger", tab, IP_RW,
                           ISR_1OFMANY, 60, IPS_IDLE);
        sprintf(name, "LINE_DIFFERENTIAL_%02d", x + 1);
        IUFillSwitchVector(&lineDifferentialSP[x], &lineDifferentialS[x * 2], 2, getDeviceName(), name, "Differential", tab, IP_RW,
                           ISR_1OFMANY, 60, IPS_IDLE);
        sprintf(name, "LINE_LOCATION_%02d", x + 1);
        IUFillNumberVector(&lineLocationNP[x], &lineLocationN[x * 3], 3, getDeviceName(), name, "Line location", tab, IP_RW, 60,
                           IPS_IDLE);
        sprintf(name, "LINE_DELAY_%02d", x + 1);
        IUFillNumberVector(&lineDelayNP[x], &lineDelayN[x], 1, getDeviceName(), name, "Delay line", tab, IP_RO, 60, IPS_IDLE);
        sprintf(name, "LINE_STATS_%02d", x + 1);
        IUFillNumberVector(&lineStatsNP[x], &lineStatsN[x * 4], 4, getDeviceName(), name, "Stats", tab, IP_RO, 60, IPS_BUSY);

        if(ahp_xc_get_crosscorrelator_lagsize() > 1)
        {
            sprintf(name, "AUTOCORRELATIONS_%02d", x + 1);
            char prefix[MAXINDILABEL - 16] = { 0 };
            if(ahp_xc_get_nlines() > 1)
                sprintf(prefix, "_%03d", x + 1);
            sprintf(label, "Autocorrelations%s", prefix);
            IUFillBLOB(&autocorrelationsB[x], name, label, ".fits");
        }

        for (unsigned int y = x + 1; y < ahp_xc_get_nlines(); y++)
        {
            if(ahp_xc_get_crosscorrelator_lagsize() > 1)
            {
                sprintf(name, "CROSSCORRELATIONS_%02d_%02d", x + 1, y + 1);
                char prefix[MAXINDILABEL - 17] = { 0 };
                if(ahp_xc_get_nbaselines() > 1)
                    sprintf(prefix, "_%03d*%03d", x + 1, y + 1);
                sprintf(label, "Crosscorrelations%s", prefix);
                IUFillBLOB(&crosscorrelationsB[idx], name, label, ".fits");
            }
            sprintf(name, "CORRELATIONS_%0d_%0d", x + 1, y + 1);
            sprintf(label, "Correlations (%d*%d)", x + 1, y + 1);
            IUFillNumber(&correlationsN[idx * 2], name, label, "%1.4f", 0, 1.0, 1, 0);
            sprintf(name, "COHERENCE_%0d_%0d", x + 1, y + 1);
            sprintf(label, "Coherence ratio (%d*%d)", x + 1, y + 1);
            IUFillNumber(&correlationsN[idx * 2 + 1], name, label, "%01.04f", 0, 1.0, 0.0001, 0);
            idx++;
        }
    }
    if(ahp_xc_get_autocorrelator_lagsize() > 1)
        IUFillBLOBVector(&autocorrelationsBP, autocorrelationsB, static_cast<int>(ahp_xc_get_nlines()), getDeviceName(),
                         "AUTOCORRELATIONS", "Autocorrelations", "Stats", IP_RO, 60, IPS_BUSY);
    if(ahp_xc_get_crosscorrelator_lagsize() > 1)
        IUFillBLOBVector(&crosscorrelationsBP, crosscorrelationsB, static_cast<int>(ahp_xc_get_nbaselines()), getDeviceName(),
                         "CROSSCORRELATIONS", "Crosscorrelations", "Stats", IP_RO, 60, IPS_BUSY);
    IUFillNumberVector(&correlationsNP, correlationsN, static_cast<int>(ahp_xc_get_nbaselines() * 2), getDeviceName(),
                       "CORRELATIONS", "Correlations", "Stats", IP_RO, 60, IPS_BUSY);

    setupParams();

    EnableCapture(true);

    return true;
}

void AHP_XC::ActiveLine(unsigned int line, bool on, bool power, bool active_low, bool edge_triggered, bool differential)
{
    ahp_xc_set_leds(line, (on ? 1 : 0) | (power ? 2 : 0) | (active_low ? 4 : 0) | (edge_triggered ? 8 : 0) | (differential ? 16 : 0));
}

void AHP_XC::EnableCapture(bool start)
{
    if(start)
        ahp_xc_set_capture_flags((xc_capture_flags)(CAP_ENABLE|CAP_RESET_TIMESTAMP));
    else
        ahp_xc_set_capture_flags((xc_capture_flags)(CAP_NONE|CAP_RESET_TIMESTAMP));
}
