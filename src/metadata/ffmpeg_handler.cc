/*MT*

    MediaTomb - http://www.mediatomb.cc/

    ffmpeg_handler.cc - this file is part of MediaTomb.

    Copyright (C) 2005 Gena Batyan <bgeradz@mediatomb.cc>,
                       Sergey 'Jin' Bostandzhyan <jin@mediatomb.cc>

    Copyright (C) 2006-2010 Gena Batyan <bgeradz@mediatomb.cc>,
                            Sergey 'Jin' Bostandzhyan <jin@mediatomb.cc>,
                            Leonhard Wimmer <leo@mediatomb.cc>

    Copyright (C) 2016-2024 Gerbera Contributors

    MediaTomb is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2
    as published by the Free Software Foundation.

    MediaTomb is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    version 2 along with MediaTomb; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.

    $Id$
*/
/*
    This code was contributed by
    Copyright (C) 2007 Ingo Preiml <ipreiml@edu.uni-klu.ac.at>
*/

/// \file ffmpeg_handler.cc

// Information about the stream are to be found in the structure
// AVFormatContext, defined in libavformat/avformat.h:335
// and in the structure
// AVCodecContext, defined in libavcodec/avcodec.h:722
// in the ffmpeg sources

#ifdef HAVE_FFMPEG
#define GRB_LOG_FAC GrbLogFacility::ffmpeg
#include "ffmpeg_handler.h"

#include "cds/cds_item.h"
#include "config/config_val.h"
#include "iohandler/io_handler.h"
#include "upnp/upnp_common.h"
#include "util/grb_time.h"
#include "util/string_converter.h"
#include "util/tools.h"

#include <fmt/chrono.h>

extern "C" {

#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

} // extern "C"

#ifdef HAVE_AVSTREAM_CODECPAR
#define as_codecpar(s) s->codecpar
#else
#define as_codecpar(s) s->codec
#endif

class FfmpegLogger {
public:
    FfmpegLogger()
    {
        av_log_set_level(AV_LOG_INFO);
        av_log_set_callback(&FfmpegLogger::LogFfmpegMessage);
    }
    ~FfmpegLogger()
    {
        av_log_set_callback(nullptr);
    }

private:
    FfmpegLogger(const FfmpegLogger&) = delete;
    FfmpegLogger& operator=(const FfmpegLogger&) = delete;
    static int printPrefix;

    static void LogFfmpegMessage(void* ptr, int level, const char* fmt, va_list vargs)
    {
        int len = av_log_format_line2(ptr, level, fmt, vargs, nullptr, 0, &printPrefix);
        std::vector<char> buf(len + 1); // need space for NUL
        std::string message;
        if (len > 0) {
            av_log_format_line2(ptr, level, fmt, vargs, buf.data(), len, &printPrefix);
            message = buf.data();
        } else {
            message = "Failed to format message";
        }

        switch (level) {
        case AV_LOG_PANIC: // ffmpeg will crash now
            log_error("FFMpeg: {}", message);
            break;
        case AV_LOG_FATAL: // fatal as in can't decode, not crash
        case AV_LOG_ERROR:
        case AV_LOG_WARNING:
            log_warning("FFMpeg: {}", message);
            break;
        case AV_LOG_INFO:
            log_info("FFMpeg: {}", message);
            break;
        case AV_LOG_VERBOSE:
        case AV_LOG_DEBUG:
        case AV_LOG_TRACE:
            log_debug("FFMpeg: {}", message);
            break;
        default:
            log_warning("FFMpeg unhandled {}: {}", message);
            break;
        }
    }
};

static FfmpegLogger globalFfmpegLogger = FfmpegLogger();
int FfmpegLogger::printPrefix = 1;

FfmpegHandler::FfmpegHandler(const std::shared_ptr<Context>& context)
    : MediaMetadataHandler(context, ConfigVal::IMPORT_LIBOPTS_FFMPEG_ENABLED, ConfigVal::IMPORT_LIBOPTS_FFMPEG_METADATA_TAGS_LIST, ConfigVal::IMPORT_LIBOPTS_FFMPEG_AUXDATA_TAGS_LIST)
{
    mappings = this->config->getDictionaryOption(ConfigVal::IMPORT_MAPPINGS_MIMETYPE_TO_CONTENTTYPE_LIST);
}

void FfmpegHandler::addFfmpegAuxdataFields(const std::shared_ptr<CdsItem>& item, const std::unique_ptr<StringConverter>& sc, const AVFormatContext* pFormatCtx) const
{
    if (!pFormatCtx->metadata) {
        log_debug("no metadata");
        return;
    }

    for (auto&& desiredTag : auxTags) {
        if (!desiredTag.empty()) {
            auto tag = av_dict_get(pFormatCtx->metadata, desiredTag.c_str(), nullptr, AV_DICT_IGNORE_SUFFIX);
            if (tag && tag->value && tag->value[0]) {
                log_debug("Added {}: {}", desiredTag.c_str(), tag->value);
                item->setAuxData(desiredTag, sc->convert(tag->value));
            }
        }
    }
} // addFfmpegAuxdataFields

void FfmpegHandler::addFfmpegMetadataFields(const std::shared_ptr<CdsItem>& item, const std::unique_ptr<StringConverter>& sc, const AVFormatContext* pFormatCtx) const
{
    AVDictionaryEntry* e = nullptr;

    // only use ffmpeg meta data if not found by other handler
    auto emptyProperties = std::map<MetadataFields, bool>();
    for (auto&& prop : propertyMap) {
        emptyProperties[prop.first] = item->getMetaData(prop.first).empty();
    }
    auto emptySpecProperties = std::map<std::string, bool>();
    for (auto&& prop : metaTags) {
        emptySpecProperties[prop.second] = item->getMetaData(prop.second).empty();
    }

    while ((e = av_dict_get(pFormatCtx->metadata, "", e, AV_DICT_IGNORE_SUFFIX))) {
        std::string key = toLower(e->key);
        std::string value = e->value;
        log_debug("FFMpeg tag: {}: {}", key, value);
        auto it = metaTags.find(e->key);
        if (it != metaTags.end() && emptySpecProperties[it->second]) {
            log_debug("Identified special metadata '{}' as '{}': '{}'", it->first, it->second, value);
            item->addMetaData(it->second, sc->convert(trimString(value)));
            continue; // iterate while loop
        }
        auto pIt = std::find_if(propertyMap.begin(), propertyMap.end(),
            [&](auto&& p) {
                return p.second == key && item->getMetaData(p.first).empty();
            });
        if (pIt != propertyMap.end()) {
            log_debug("Identified default metadata '{}': {}", pIt->second, value);
            const auto field = pIt->first;
            if (emptyProperties[field]) {
                if (field == MetadataFields::M_DATE || field == MetadataFields::M_CREATION_DATE) {
                    std::tm tmWork {};
                    if (strptime(e->value, "%Y-%m-%dT%T.000000%Z", &tmWork)) {
                        // convert creation_time to local time
                        auto utcTime = timegm(&tmWork);
                        if (utcTime == -1) {
                            continue;
                        }
                        tmWork = fmt::localtime(utcTime);
                    } else if (strptime(e->value, "%Y-%m-%d", &tmWork)) {
                        ; // use the value as is
                    } else if (strptime(e->value, "%Y", &tmWork)) {
                        // convert the value to "XXXX-01-01"
                        tmWork.tm_mon = 0; // Month (0-11)
                        tmWork.tm_mday = 1; // Day of the month (1-31)
                    } else
                        continue;
                    auto mDate = fmt::format("{:%Y-%m-%d}", tmWork);
                    item->addMetaData(field, mDate);
                } else
                    item->addMetaData(field, sc->convert(trimString(value)));
            }
            if (field == MetadataFields::M_TRACKNUMBER) {
                item->setTrackNumber(stoiString(value));
            } else if (field == MetadataFields::M_PARTNUMBER) {
                item->setPartNumber(stoiString(value));
            }
        } else {
            log_debug("Unhandled metadata {} = '{}'", key, value);
        }
    }
}

// ffmpeg library calls
void FfmpegHandler::addFfmpegResourceFields(const std::shared_ptr<CdsItem>& item, const AVFormatContext* pFormatCtx)
{
    auto resource = item->getResource(ContentHandler::DEFAULT);
    bool isAudioFile = item->isSubClass(UPNP_CLASS_AUDIO_ITEM) && item->getResource(ResourcePurpose::Thumbnail);
    auto resource2 = isAudioFile ? item->getResource(ResourcePurpose::Thumbnail) : item->getResource(ContentHandler::DEFAULT);

    // duration
    if (pFormatCtx->duration > 0) {
        auto duration = millisecondsToHMSF(pFormatCtx->duration / (AV_TIME_BASE / 1000));
        log_debug("Added duration: {}", duration);
        resource->addAttribute(ResourceAttribute::DURATION, duration);
    }

    // bitrate
    if (pFormatCtx->bit_rate > 0) {
        // ffmpeg's bit_rate is in bits/sec, upnp wants it in bytes/sec
        // See http://www.upnp.org/schemas/av/didl-lite-v3.xsd
        auto bitrate = pFormatCtx->bit_rate / 8;
        log_debug("Added bitrate: {} kb/s", bitrate);
        resource->addAttribute(ResourceAttribute::BITRATE, fmt::to_string(bitrate));
    }

    // video resolution, audio sampling rate, nr of audio channels
    bool audioSet = false;
    bool videoSet = false;
    for (std::size_t i = 0; i < pFormatCtx->nb_streams; i++) {
        auto st = pFormatCtx->streams[i];

        if (st && !videoSet && as_codecpar(st)->codec_type == AVMEDIA_TYPE_VIDEO) {
            auto codecId = as_codecpar(st)->codec_id;
            resource2->addAttribute(ResourceAttribute::VIDEOCODEC, avcodec_get_name(codecId));

            if (as_codecpar(st)->codec_tag > 0) {
                char fourcc[5];
                fourcc[0] = as_codecpar(st)->codec_tag;
                fourcc[1] = as_codecpar(st)->codec_tag >> 8;
                fourcc[2] = as_codecpar(st)->codec_tag >> 16;
                fourcc[3] = as_codecpar(st)->codec_tag >> 24;
                fourcc[4] = '\0';

                log_debug("FourCC: 0x{:x} = {} from stream {}", as_codecpar(st)->codec_tag, fourcc, i);
                std::string fcc = fourcc;
                if (!fcc.empty())
                    resource->addOption(RESOURCE_OPTION_FOURCC, fcc);
            }

            if (as_codecpar(st)->width > 0 && as_codecpar(st)->height > 0) {
                auto resolution = fmt::format("{}x{}", as_codecpar(st)->width, as_codecpar(st)->height);

                log_debug("Added resolution: {} pixel from stream {}", resolution, i);
                resource2->addAttribute(ResourceAttribute::RESOLUTION, resolution);
                videoSet = true;
            }
        }
        if (st && !audioSet && as_codecpar(st)->codec_type == AVMEDIA_TYPE_AUDIO) {
            auto codecId = as_codecpar(st)->codec_id;
            resource->addAttribute(ResourceAttribute::AUDIOCODEC, avcodec_get_name(codecId));
            // find the first stream that has a valid sample rate

            if (as_codecpar(st)->sample_rate > 0) {
                int sampleFreq = as_codecpar(st)->sample_rate;

                log_debug("Bits per raw sample: {}", as_codecpar(st)->bits_per_raw_sample);
                int bitsPerSample = as_codecpar(st)->bits_per_coded_sample;
                log_debug("Bits per coded sample: {}", bitsPerSample);

                if (bitsPerSample > 0) {
                    resource->addAttribute(ResourceAttribute::BITS_PER_SAMPLE, fmt::to_string(bitsPerSample));

                    // Fix up Sample rate reporting
                    // FFMpeg will tell us DSD is 16bit PCM, but its actually 1bit, so we have to multiply this out
                    if (codecId == AV_CODEC_ID_DSD_LSBF || codecId == AV_CODEC_ID_DSD_MSBF || codecId == AV_CODEC_ID_DSD_LSBF_PLANAR || codecId == AV_CODEC_ID_DSD_MSBF_PLANAR) {
                        sampleFreq *= bitsPerSample;
                    }
                }

                log_debug("Added sample frequency: {} Hz from stream {}", sampleFreq, i);
                resource->addAttribute(ResourceAttribute::SAMPLEFREQUENCY, fmt::to_string(sampleFreq));
                audioSet = true;
// FF_API_OLD_CHANNEL_LAYOUT
#if LIBAVUTIL_VERSION_MAJOR < 57
                int audioCh = as_codecpar(st)->channels;
#else
                int audioCh = as_codecpar(st)->ch_layout.nb_channels;
#endif
                if (audioCh > 0) {
                    log_debug("Added number of audio channels: {} from stream {}", audioCh, i);
                    resource->addAttribute(ResourceAttribute::NRAUDIOCHANNELS, fmt::to_string(audioCh));
                }
            }
        }
    }
}

void FfmpegHandler::fillMetadata(const std::shared_ptr<CdsObject>& obj)
{
    auto item = std::dynamic_pointer_cast<CdsItem>(obj);
    if (!item || !isEnabled)
        return;

    log_debug("Running ffmpeg handler on {}", item->getLocation().c_str());

    AVFormatContext* pFormatCtx = nullptr;

#if (LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 9, 100))
    // Register all formats and codecs
    av_register_all();
#endif
    // Open video file
    if (avformat_open_input(&pFormatCtx, item->getLocation().c_str(), nullptr, nullptr) != 0)
        return; // Couldn't open file

    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
        avformat_close_input(&pFormatCtx);
        return; // Couldn't find stream information
    }
    auto sc = StringConverter::m2i(ConfigVal::IMPORT_LIBOPTS_FFMPEG_CHARSET, item->getLocation(), config);
    // Add metadata using ffmpeg library calls
    addFfmpegMetadataFields(item, sc, pFormatCtx);
    // Add auxdata
    addFfmpegAuxdataFields(item, sc, pFormatCtx);
    // Add resources using ffmpeg library calls
    addFfmpegResourceFields(item, pFormatCtx);
    // Close the video file
    avformat_close_input(&pFormatCtx);
}

std::unique_ptr<IOHandler> FfmpegHandler::serveContent(const std::shared_ptr<CdsObject>& obj, const std::shared_ptr<CdsResource>& resource)
{
    return nullptr;
}

std::string FfmpegHandler::getMimeType() const
{
    return getValueOrDefault(mappings, CONTENT_TYPE_JPG, "image/jpeg");
}
#endif // HAVE_FFMPEG
