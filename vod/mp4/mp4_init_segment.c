#include "../media_set.h"
#include "mp4_defs.h"
#include "mp4_init_segment.h"
#include "mp4_write_stream.h"

// macros
#define mp4_rescale_millis(millis, timescale) (millis * ((timescale) / 1000))
#define mp4_esds_atom_size(extra_data_len) (ATOM_HEADER_SIZE + 29 + extra_data_len)
#define mp4_copy_atom(p, raw_atom) vod_copy(p, (raw_atom).ptr, (raw_atom).size)

// init mp4 atoms
typedef struct {
	size_t stsd_size;
	size_t stbl_size;
	size_t minf_size;
	size_t mdia_size;
	size_t trak_size;
} track_sizes_t;

typedef struct {
	size_t moov_atom_size;
	size_t mvex_atom_size;
	size_t total_size;
	size_t track_count;
	track_sizes_t track_sizes[1];
} init_mp4_sizes_t;

typedef struct {
	u_char version[1];
	u_char flags[3];
	u_char track_id[4];
	u_char default_sample_description_index[4];
	u_char default_sample_duration[4];
	u_char default_sample_size[4];
	u_char default_sample_flags[4];
} trex_atom_t;

// fixed atoms

static const u_char ftyp_atom[] = {
	0x00, 0x00, 0x00, 0x18,		// atom size
	0x66, 0x74, 0x79, 0x70,		// ftyp
	0x69, 0x73, 0x6f, 0x6d,		// major brand
	0x00, 0x00, 0x00, 0x01,		// minor version
	0x69, 0x73, 0x6f, 0x6d,		// compatible brand
	0x61, 0x76, 0x63, 0x31,		// compatible brand
};

static const u_char ftyp_atom_v2[] = {
	0x00, 0x00, 0x00, 0x1c,		// atom size
	0x66, 0x74, 0x79, 0x70,		// ftyp
	0x69, 0x73, 0x6f, 0x35,		// major brand
	0x00, 0x00, 0x00, 0x01,		// minor version
	0x69, 0x73, 0x6f, 0x35,		// compatible brand
	0x64, 0x61, 0x73, 0x68,		// compatible brand
	0x6d, 0x73, 0x69, 0x78,		// compatible brand
};
static const u_char hdlr_video_atom[] = {
	0x00, 0x00, 0x00, 0x2d,		// size
	0x68, 0x64, 0x6c, 0x72,		// hdlr
	0x00, 0x00, 0x00, 0x00,		// version + flags
	0x00, 0x00, 0x00, 0x00,		// pre defined
	0x76, 0x69, 0x64, 0x65,		// handler type = vide
	0x00, 0x00, 0x00, 0x00,		// reserved1
	0x00, 0x00, 0x00, 0x00,		// reserved2
	0x00, 0x00, 0x00, 0x00,		// reserved3
	0x56, 0x69, 0x64, 0x65,		// VideoHandler\0
	0x6f, 0x48, 0x61, 0x6e,
	0x64, 0x6c, 0x65, 0x72,
	0x00
};

static const u_char hdlr_audio_atom[] = {
	0x00, 0x00, 0x00, 0x2d,		// size
	0x68, 0x64, 0x6c, 0x72,		// hdlr
	0x00, 0x00, 0x00, 0x00,		// version + flags
	0x00, 0x00, 0x00, 0x00,		// pre defined
	0x73, 0x6f, 0x75, 0x6e,		// handler type = soun
	0x00, 0x00, 0x00, 0x00,		// reserved1
	0x00, 0x00, 0x00, 0x00,		// reserved2
	0x00, 0x00, 0x00, 0x00,		// reserved3
	0x53, 0x6f, 0x75, 0x6e,		// name = SoundHandler\0
	0x64, 0x48, 0x61, 0x6e,
	0x64, 0x6c, 0x65, 0x72,
	0x00
};

static const u_char dinf_atom[] = {
	0x00, 0x00, 0x00, 0x24,		// atom size
	0x64, 0x69, 0x6e, 0x66,		// dinf
	0x00, 0x00, 0x00, 0x1c,		// atom size
	0x64, 0x72, 0x65, 0x66,		// dref
	0x00, 0x00, 0x00, 0x00,		// version + flags
	0x00, 0x00, 0x00, 0x01,		// entry count
	0x00, 0x00, 0x00, 0x0c,		// atom size
	0x75, 0x72, 0x6c, 0x20,		// url
	0x00, 0x00, 0x00, 0x01,		// version + flags
};

static const u_char vmhd_atom[] = {
	0x00, 0x00, 0x00, 0x14,		// atom size
	0x76, 0x6d, 0x68, 0x64,		// vmhd
	0x00, 0x00, 0x00, 0x01,		// version & flags
	0x00, 0x00, 0x00, 0x00,		// reserved
	0x00, 0x00, 0x00, 0x00,		// reserved
};

static const u_char smhd_atom[] = {
	0x00, 0x00, 0x00, 0x10,		// atom size
	0x73, 0x6d, 0x68, 0x64,		// smhd
	0x00, 0x00, 0x00, 0x00,		// version & flags
	0x00, 0x00, 0x00, 0x00,		// reserved
};

static const u_char fixed_stbl_atoms[] = {
	0x00, 0x00, 0x00, 0x10,		// atom size
	0x73, 0x74, 0x74, 0x73,		// stts
	0x00, 0x00, 0x00, 0x00,		// version
	0x00, 0x00, 0x00, 0x00,		// entry count
	0x00, 0x00, 0x00, 0x10,		// atom size
	0x73, 0x74, 0x73, 0x63,		// stsc
	0x00, 0x00, 0x00, 0x00,		// version
	0x00, 0x00, 0x00, 0x00,		// entry count
	0x00, 0x00, 0x00, 0x14,		// atom size
	0x73, 0x74, 0x73, 0x7a,		// stsz
	0x00, 0x00, 0x00, 0x00,		// version
	0x00, 0x00, 0x00, 0x00,		// uniform size
	0x00, 0x00, 0x00, 0x00,		// entry count
	0x00, 0x00, 0x00, 0x10, 	// atom size
	0x73, 0x74, 0x63, 0x6f,		// stco
	0x00, 0x00, 0x00, 0x00,		// version
	0x00, 0x00, 0x00, 0x00,		// entry count
};


static void
mp4_init_segment_get_track_sizes(
	media_set_t* media_set, 
	media_track_t* cur_track, 
	atom_writer_t* stsd_atom_writer,
	track_sizes_t* result)
{
	uint32_t timescale = media_set->filtered_tracks->media_info.timescale;
	size_t tkhd_atom_size;
	size_t mdhd_atom_size;
	size_t hdlr_atom_size = 0;

	if (stsd_atom_writer != NULL)
	{
		result->stsd_size = stsd_atom_writer->atom_size;
	}
	else
	{
		result->stsd_size = cur_track->raw_atoms[RTA_STSD].size;
	}

	if (media_set->type != MEDIA_SET_LIVE && 
		mp4_rescale_millis(media_set->timing.total_duration, timescale) > UINT_MAX)
	{
		tkhd_atom_size = ATOM_HEADER_SIZE + sizeof(tkhd64_atom_t);
		mdhd_atom_size = ATOM_HEADER_SIZE + sizeof(mdhd64_atom_t);
	}
	else
	{
		tkhd_atom_size = ATOM_HEADER_SIZE + sizeof(tkhd_atom_t);
		mdhd_atom_size = ATOM_HEADER_SIZE + sizeof(mdhd_atom_t);
	}

	result->stbl_size = ATOM_HEADER_SIZE + result->stsd_size + sizeof(fixed_stbl_atoms);
	result->minf_size = ATOM_HEADER_SIZE + sizeof(dinf_atom) + result->stbl_size;
	switch (cur_track->media_info.media_type)
	{
	case MEDIA_TYPE_VIDEO:
		result->minf_size += sizeof(vmhd_atom);
		hdlr_atom_size = sizeof(hdlr_video_atom);
		break;
	case MEDIA_TYPE_AUDIO:
		result->minf_size += sizeof(smhd_atom);
		hdlr_atom_size = sizeof(hdlr_audio_atom);
		break;
	}
	result->mdia_size = ATOM_HEADER_SIZE + mdhd_atom_size + hdlr_atom_size + result->minf_size;
	result->trak_size = ATOM_HEADER_SIZE + tkhd_atom_size + result->mdia_size;
}

static u_char*
mp4_init_segment_write_trex_atom(u_char* p, uint32_t track_id)
{
	size_t atom_size = ATOM_HEADER_SIZE + sizeof(trex_atom_t);

	write_atom_header(p, atom_size, 't', 'r', 'e', 'x');
	write_be32(p, 0);			// version + flags
	write_be32(p, track_id);	// track id
	write_be32(p, 1);			// default sample description index
	write_be32(p, 0);			// default sample duration
	write_be32(p, 0);			// default sample size
	write_be32(p, 0);			// default sample size
	return p;
}

static u_char* 
mp4_init_segment_write_matrix(u_char* p, int16_t a, int16_t b, int16_t c,
	int16_t d, int16_t tx, int16_t ty)
{
	write_be32(p, a << 16);  // 16.16 format
	write_be32(p, b << 16);  // 16.16 format
	write_be32(p, 0);        // u in 2.30 format
	write_be32(p, c << 16);  // 16.16 format
	write_be32(p, d << 16);  // 16.16 format
	write_be32(p, 0);        // v in 2.30 format
	write_be32(p, tx << 16); // 16.16 format
	write_be32(p, ty << 16); // 16.16 format
	write_be32(p, 1 << 30);  // w in 2.30 format
	return p;
}

static u_char*
mp4_init_segment_write_mvhd_constants(u_char* p)
{
	write_be32(p, 0x00010000);	// preferred rate, 1.0
	write_be16(p, 0x0100);		// volume, full
	write_be16(p, 0);			// reserved
	write_be32(p, 0);			// reserved
	write_be32(p, 0);			// reserved
	p = mp4_init_segment_write_matrix(p, 1, 0, 0, 1, 0, 0);	// matrix
	write_be32(p, 0);			// reserved (preview time)
	write_be32(p, 0);			// reserved (preview duration)
	write_be32(p, 0);			// reserved (poster time)
	write_be32(p, 0);			// reserved (selection time)
	write_be32(p, 0);			// reserved (selection duration)
	write_be32(p, 0);			// reserved (current time)
	return p;
}

static u_char*
mp4_init_segment_write_mvhd_atom(u_char* p, uint32_t timescale, uint32_t duration)
{
	size_t atom_size = ATOM_HEADER_SIZE + sizeof(mvhd_atom_t);

	write_atom_header(p, atom_size, 'm', 'v', 'h', 'd');
	write_be32(p, 0);			// version + flags
	write_be32(p, 0);			// creation time
	write_be32(p, 0);			// modification time
	write_be32(p, timescale);	// timescale
	write_be32(p, duration);	// duration
	p = mp4_init_segment_write_mvhd_constants(p);
	write_be32(p, 0xffffffff);	// next track id
	return p;
}

static u_char*
mp4_init_segment_write_mvhd64_atom(u_char* p, uint32_t timescale, uint64_t duration)
{
	size_t atom_size = ATOM_HEADER_SIZE + sizeof(mvhd64_atom_t);

	write_atom_header(p, atom_size, 'm', 'v', 'h', 'd');
	write_be32(p, 0x01000000);	// version + flags
	write_be64(p, 0LL);			// creation time
	write_be64(p, 0LL);			// modification time
	write_be32(p, timescale);	// timescale
	write_be64(p, duration);	// duration
	p = mp4_init_segment_write_mvhd_constants(p);
	write_be32(p, 0xffffffff);	// next track id
	return p;
}

static u_char*
mp4_init_segment_write_tkhd_trailer(
	u_char* p, 
	uint32_t media_type, 
	uint16_t width, 
	uint16_t height)
{
	write_be32(p, 0);				// reserved
	write_be32(p, 0);				// reserved
	write_be32(p, 0);				// layer / alternate group
	write_be16(p, media_type == MEDIA_TYPE_AUDIO ? 0x0100 : 0);		// volume
	write_be16(p, 0);				// reserved
	p = mp4_init_segment_write_matrix(p, 1, 0, 0, 1, 0, 0);	// matrix
	if (media_type == MEDIA_TYPE_VIDEO)
	{
		write_be32(p, width << 16);		// width
		write_be32(p, height << 16);	// height
	}
	else
	{
		write_be32(p, 0);			// width
		write_be32(p, 0);			// height
	}
	return p;
}

static u_char*
mp4_init_segment_write_tkhd_atom(
	u_char* p, 
	uint32_t track_id,
	uint32_t duration, 
	uint32_t media_type, 
	uint16_t width, 
	uint16_t height)
{
	size_t atom_size = ATOM_HEADER_SIZE + sizeof(tkhd_atom_t);

	write_atom_header(p, atom_size, 't', 'k', 'h', 'd');
	write_be32(p, 0x00000003);		// version + flags
	write_be32(p, 0);				// creation time
	write_be32(p, 0);				// modification time
	write_be32(p, track_id);		// track id
	write_be32(p, 0);				// reserved
	write_be32(p, duration);		// duration
	return mp4_init_segment_write_tkhd_trailer(p, media_type, width, height);
}

static u_char*
mp4_init_segment_write_tkhd64_atom(
	u_char* p, 
	uint32_t track_id,
	uint64_t duration,
	uint32_t media_type, 
	uint16_t width, 
	uint16_t height)
{
	size_t atom_size = ATOM_HEADER_SIZE + sizeof(tkhd64_atom_t);

	write_atom_header(p, atom_size, 't', 'k', 'h', 'd');
	write_be32(p, 0x01000003);		// version + flags
	write_be64(p, 0LL);				// creation time
	write_be64(p, 0LL);				// modification time
	write_be32(p, track_id);		// track id
	write_be32(p, 0);				// reserved
	write_be64(p, duration);		// duration
	return mp4_init_segment_write_tkhd_trailer(p, media_type, width, height);
}

static u_char*
mp4_init_segment_write_mdhd_atom(u_char* p, uint32_t timescale, uint32_t duration)
{
	size_t atom_size = ATOM_HEADER_SIZE + sizeof(mdhd_atom_t);

	write_atom_header(p, atom_size, 'm', 'd', 'h', 'd');
	write_be32(p, 0);				// version + flags
	write_be32(p, 0);				// creation time
	write_be32(p, 0);				// modification time
	write_be32(p, timescale);		// timescale
	write_be32(p, duration);		// duration
	write_be16(p, 0);				// language
	write_be16(p, 0);				// reserved
	return p;
}

static u_char*
mp4_init_segment_write_mdhd64_atom(u_char* p, uint32_t timescale, uint64_t duration)
{
	size_t atom_size = ATOM_HEADER_SIZE + sizeof(mdhd64_atom_t);

	write_atom_header(p, atom_size, 'm', 'd', 'h', 'd');
	write_be32(p, 0x01000000);		// version + flags
	write_be64(p, 0LL);				// creation time
	write_be64(p, 0LL);				// modification time
	write_be32(p, timescale);		// timescale
	write_be64(p, duration);		// duration
	write_be16(p, 0);				// language
	write_be16(p, 0);				// reserved
	return p;
}

static u_char*
mp4_init_segment_write_avcc_atom(u_char* p, media_track_t* track)
{
	size_t atom_size = ATOM_HEADER_SIZE + track->media_info.extra_data.len;

	write_atom_header(p, atom_size, 'a', 'v', 'c', 'C');
	p = vod_copy(p, track->media_info.extra_data.data, track->media_info.extra_data.len);
	return p;
}

static u_char*
mp4_init_segment_write_stsd_video_entry(u_char* p, media_track_t* track)
{
	size_t atom_size = ATOM_HEADER_SIZE + sizeof(sample_entry_t) + sizeof(stsd_video_t) +
		ATOM_HEADER_SIZE + track->media_info.extra_data.len;

	write_atom_header(p, atom_size, 'a', 'v', 'c', '1');

	// sample_entry_t
	write_be32(p, 0);		// reserved
	write_be16(p, 0);		// reserved
	write_be16(p, 1);		// data reference index

	// stsd_video_t
	write_be16(p, 0);		// pre defined
	write_be16(p, 0);		// reserved
	write_be32(p, 0);		// pre defined
	write_be32(p, 0);		// pre defined
	write_be32(p, 0);		// pre defined
	write_be16(p, track->media_info.u.video.width);
	write_be16(p, track->media_info.u.video.height);
	write_be32(p, 0x00480000);	// horiz res (72 DPI)
	write_be32(p, 0x00480000);	// vert res (72 DPI)
	write_be32(p, 0);		// reserved
	write_be16(p, 1);		// frame count
	vod_memzero(p, 32);		// compressor name
	p += 32;
	write_be16(p, 0x18);	// depth
	write_be16(p, 0xffff);	// pre defined

	p = mp4_init_segment_write_avcc_atom(p, track);

	return p;
}

static u_char*
mp4_init_segment_write_esds_atom(u_char* p, media_track_t* track)
{
	size_t extra_data_len = track->media_info.extra_data.len;
	size_t atom_size = mp4_esds_atom_size(extra_data_len);

	write_atom_header(p, atom_size, 'e', 's', 'd', 's');
	write_be32(p, 0);							// version + flags

	*p++ = MP4ESDescrTag;						// tag
	*p++ = 3 + 3 * sizeof(descr_header_t) +		// len
		sizeof(config_descr_t) + extra_data_len + 1;
	write_be16(p, 1);							// track id
	*p++ = 0;									// flags

	*p++ = MP4DecConfigDescrTag;				// tag
	*p++ = sizeof(config_descr_t) +				// len
		sizeof(descr_header_t) + extra_data_len;
	*p++ = track->media_info.u.audio.object_type_id;
	*p++ = 0x15;								// stream type
	write_be24(p, 0);							// buffer size
	write_be32(p, track->media_info.bitrate);	// max bitrate
	write_be32(p, track->media_info.bitrate);	// avg bitrate

	*p++ = MP4DecSpecificDescrTag;				// tag
	*p++ = extra_data_len;						// len
	p = vod_copy(p, track->media_info.extra_data.data, extra_data_len);

	*p++ = MP4SLDescrTag;						// tag
	*p++ = 1;									// len
	*p++ = 2;

	return p;
}

static u_char*
mp4_init_segment_write_stsd_audio_entry(u_char* p, media_track_t* track)
{
	size_t atom_size = ATOM_HEADER_SIZE + sizeof(sample_entry_t) + sizeof(stsd_audio_t) +
		mp4_esds_atom_size(track->media_info.extra_data.len);

	write_atom_header(p, atom_size, 'm', 'p', '4', 'a');

	// sample_entry_t
	write_be32(p, 0);		// reserved
	write_be16(p, 0);		// reserved
	write_be16(p, 1);		// data reference index

	// stsd_audio_t
	write_be32(p, 0);		// reserved
	write_be32(p, 0);		// reserved
	write_be16(p, track->media_info.u.audio.channels);
	write_be16(p, track->media_info.u.audio.bits_per_sample);
	write_be16(p, 0);		// pre defined
	write_be16(p, 0);		// reserved
	write_be16(p, track->media_info.u.audio.sample_rate);
	write_be16(p, 0);

	p = mp4_init_segment_write_esds_atom(p, track);

	return p;
}

static size_t
mp4_init_segment_get_stsd_atom_size(media_track_t* track)
{
	size_t atom_size = ATOM_HEADER_SIZE + sizeof(stsd_atom_t);

	switch (track->media_info.media_type)
	{
	case MEDIA_TYPE_VIDEO:
		atom_size += ATOM_HEADER_SIZE + sizeof(sample_entry_t) + sizeof(stsd_video_t)+
			ATOM_HEADER_SIZE + track->media_info.extra_data.len;
		break;

	case MEDIA_TYPE_AUDIO:
		atom_size += ATOM_HEADER_SIZE + sizeof(sample_entry_t) + sizeof(stsd_audio_t)+
			mp4_esds_atom_size(track->media_info.extra_data.len);
		break;
	}

	return atom_size;
}

static u_char*
mp4_init_segment_write_stsd_atom(u_char* p, size_t atom_size, media_track_t* track)
{
	write_atom_header(p, atom_size, 's', 't', 's', 'd');
	write_be32(p, 0);				// version + flags
	write_be32(p, 1);				// entries
	switch (track->media_info.media_type)
	{
	case MEDIA_TYPE_VIDEO:
		p = mp4_init_segment_write_stsd_video_entry(p, track);
		break;

	case MEDIA_TYPE_AUDIO:
		p = mp4_init_segment_write_stsd_audio_entry(p, track);
		break;
	}
	return p;
}

static void
mp4_init_segment_calc_size(
	media_set_t* media_set,
	atom_writer_t* extra_moov_atoms_writer,
	atom_writer_t* stsd_atom_writer, 
	init_mp4_sizes_t* result)
{
	media_track_t* first_track = media_set->filtered_tracks;
	media_track_t* last_track = first_track + media_set->total_track_count;
	media_track_t* cur_track;
	track_sizes_t* track_sizes;
	uint32_t timescale = first_track->media_info.timescale;

	result->mvex_atom_size = ATOM_HEADER_SIZE + 
		(ATOM_HEADER_SIZE + sizeof(trex_atom_t)) * media_set->total_track_count;

	result->moov_atom_size = ATOM_HEADER_SIZE + result->mvex_atom_size;

	if (media_set->type != MEDIA_SET_LIVE && 
		mp4_rescale_millis(media_set->timing.total_duration, timescale) > UINT_MAX)
	{
		result->moov_atom_size += ATOM_HEADER_SIZE + sizeof(mvhd64_atom_t);
	}
	else
	{
		result->moov_atom_size += ATOM_HEADER_SIZE + sizeof(mvhd_atom_t);
	}

	if (extra_moov_atoms_writer != NULL)
	{
		result->moov_atom_size += extra_moov_atoms_writer->atom_size;
	}

	for (cur_track = first_track, track_sizes = result->track_sizes;
		cur_track < last_track; 
		cur_track++, track_sizes++)
	{
		mp4_init_segment_get_track_sizes(media_set, cur_track, stsd_atom_writer, track_sizes);

		result->moov_atom_size += track_sizes->trak_size;
	}

	result->total_size = 
		(media_set->version >= 2 ? sizeof(ftyp_atom_v2) : sizeof(ftyp_atom)) + 
		result->moov_atom_size;
}

static u_char*
mp4_init_segment_write(
	u_char* p,
	request_context_t* request_context,
	media_set_t* media_set,
	init_mp4_sizes_t* sizes,
	atom_writer_t* extra_moov_atoms_writer,
	atom_writer_t* stsd_atom_writer)
{
	media_track_t* first_track = media_set->filtered_tracks;
	media_track_t* cur_track;
	track_sizes_t* track_sizes;
	uint32_t timescale = first_track->media_info.timescale;
	uint64_t duration;
	uint32_t i;

	if (media_set->type == MEDIA_SET_LIVE)
	{
		duration = 0;
	}
	else
	{
		duration = mp4_rescale_millis(media_set->timing.total_duration, timescale);
	}

	// ftyp
	if (media_set->version >= 2)
	{
		p = vod_copy(p, ftyp_atom_v2, sizeof(ftyp_atom_v2));
	}
	else
	{
		p = vod_copy(p, ftyp_atom, sizeof(ftyp_atom));
	}

	// moov
	write_atom_header(p, sizes->moov_atom_size, 'm', 'o', 'o', 'v');

	// moov.mvhd
	if (duration > UINT_MAX)
	{
		p = mp4_init_segment_write_mvhd64_atom(p, timescale, duration);
	}
	else
	{
		p = mp4_init_segment_write_mvhd_atom(p, timescale, duration);
	}

	// moov.mvex
	write_atom_header(p, sizes->mvex_atom_size, 'm', 'v', 'e', 'x');

	for (i = 0; i < media_set->total_track_count; i++)
	{
		// moov.mvex.trex
		p = mp4_init_segment_write_trex_atom(p, i + 1);
	}

	for (i = 0; i < media_set->total_track_count; i++)
	{
		cur_track = &first_track[i];
		track_sizes = &sizes->track_sizes[i];

		// moov.trak
		write_atom_header(p, track_sizes->trak_size, 't', 'r', 'a', 'k');

		// moov.trak.tkhd
		if (duration > UINT_MAX)
		{
			p = mp4_init_segment_write_tkhd64_atom(
				p,
				i + 1,
				duration,
				cur_track->media_info.media_type,
				cur_track->media_info.u.video.width,
				cur_track->media_info.u.video.height);
		}
		else
		{
			p = mp4_init_segment_write_tkhd_atom(
				p,
				i + 1,
				duration,
				cur_track->media_info.media_type,
				cur_track->media_info.u.video.width,
				cur_track->media_info.u.video.height);
		}

		// moov.trak.mdia
		write_atom_header(p, track_sizes->mdia_size, 'm', 'd', 'i', 'a');

		// moov.trak.mdia.mdhd
		if (duration > UINT_MAX)
		{
			p = mp4_init_segment_write_mdhd64_atom(p, timescale, duration);
		}
		else
		{
			p = mp4_init_segment_write_mdhd_atom(p, timescale, duration);
		}

		// moov.trak.mdia.hdlr
		switch (cur_track->media_info.media_type)
		{
		case MEDIA_TYPE_VIDEO:
			p = vod_copy(p, hdlr_video_atom, sizeof(hdlr_video_atom));
			break;
		case MEDIA_TYPE_AUDIO:
			p = vod_copy(p, hdlr_audio_atom, sizeof(hdlr_audio_atom));
			break;
		}

		// moov.trak.mdia.minf
		write_atom_header(p, track_sizes->minf_size, 'm', 'i', 'n', 'f');
		switch (cur_track->media_info.media_type)
		{
		case MEDIA_TYPE_VIDEO:
			p = vod_copy(p, vmhd_atom, sizeof(vmhd_atom));
			break;
		case MEDIA_TYPE_AUDIO:
			p = vod_copy(p, smhd_atom, sizeof(smhd_atom));
			break;
		}
		p = vod_copy(p, dinf_atom, sizeof(dinf_atom));

		// moov.trak.mdia.minf.stbl
		write_atom_header(p, track_sizes->stbl_size, 's', 't', 'b', 'l');
		if (stsd_atom_writer != NULL)
		{
			p = stsd_atom_writer->write(stsd_atom_writer->context, p);
		}
		else
		{
			p = mp4_copy_atom(p, cur_track->raw_atoms[RTA_STSD]);
		}
		p = vod_copy(p, fixed_stbl_atoms, sizeof(fixed_stbl_atoms));
	}

	// moov.xxx
	if (extra_moov_atoms_writer != NULL)
	{
		p = extra_moov_atoms_writer->write(extra_moov_atoms_writer->context, p);
	}

	return p;
}

vod_status_t
mp4_init_segment_build_stsd_atom(
	request_context_t* request_context,
	media_track_t* track)
{
	size_t atom_size;
	u_char* p;

	atom_size = mp4_init_segment_get_stsd_atom_size(track);
	p = vod_alloc(request_context->pool, atom_size);
	if (p == NULL)
	{
		vod_log_debug0(VOD_LOG_DEBUG_LEVEL, request_context->log, 0,
			"mp4_init_segment_build_stsd_atom: vod_alloc failed (1)");
		return VOD_ALLOC_FAILED;
	}

	track->raw_atoms[RTA_STSD].ptr = p;
	track->raw_atoms[RTA_STSD].size =
		mp4_init_segment_write_stsd_atom(p, atom_size, track) - p;

	if (track->raw_atoms[RTA_STSD].size > atom_size)
	{
		vod_log_error(VOD_LOG_ERR, request_context->log, 0,
			"mp4_init_segment_build_stsd_atom: stsd length %uL greater than allocated length %uz",
			track->raw_atoms[RTA_STSD].size, atom_size);
		return VOD_UNEXPECTED;
	}

	return VOD_OK;
}

vod_status_t 
mp4_init_segment_build(
	request_context_t* request_context,
	media_set_t* media_set,
	bool_t size_only,
	atom_writer_t* extra_moov_atoms_writer,
	atom_writer_t* stsd_atom_writer,
	vod_str_t* result)
{
	media_track_t* first_track = media_set->filtered_tracks;
	media_track_t* last_track = first_track + media_set->total_track_count;
	media_track_t* cur_track;
	init_mp4_sizes_t* sizes;
	vod_status_t rc;
	u_char* p;

	// create an stsd atom if needed
	for (cur_track = first_track; cur_track < last_track; cur_track++)
	{
		if (cur_track->raw_atoms[RTA_STSD].size != 0)
		{
			continue;
		}

		rc = mp4_init_segment_build_stsd_atom(request_context, cur_track);
		if (rc != VOD_OK)
		{
			return rc;
		}
	}

	// get the result size
	sizes = vod_alloc(request_context->pool, sizeof(*sizes) + 
		sizeof(sizes->track_sizes[0]) * (media_set->total_track_count - 1));
	if (sizes == NULL)
	{
		vod_log_debug0(VOD_LOG_DEBUG_LEVEL, request_context->log, 0,
			"mp4_init_segment_build: vod_alloc failed (1)");
		return VOD_ALLOC_FAILED;
	}

	mp4_init_segment_calc_size(
		media_set,
		extra_moov_atoms_writer,
		stsd_atom_writer,
		sizes);

	// head request optimization
	if (size_only)
	{
		result->len = sizes->total_size;
		return VOD_OK;
	}

	// allocate the buffer
	result->data = vod_alloc(request_context->pool, sizes->total_size);
	if (result->data == NULL)
	{
		vod_log_debug0(VOD_LOG_DEBUG_LEVEL, request_context->log, 0,
			"mp4_init_segment_build: vod_alloc failed (2)");
		return VOD_ALLOC_FAILED;
	}

	// write the init mp4
	p = mp4_init_segment_write(
		result->data,
		request_context,
		media_set,
		sizes,
		extra_moov_atoms_writer,
		stsd_atom_writer);

	result->len = p - result->data;

	if (result->len != sizes->total_size)
	{
		vod_log_error(VOD_LOG_ERR, request_context->log, 0,
			"mp4_init_segment_build: result length %uz different than allocated length %uz",
			result->len, sizes->total_size);
		return VOD_UNEXPECTED;
	}

	return VOD_OK;
}