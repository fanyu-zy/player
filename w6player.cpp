#include <iostream>
#include <cstdlib>
#include <queue>
#include <thread>
#include <string>
#define __STDC_CONSTANT_MACROS
#define SDL_MAIN_HANDLED
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_main.h>
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
}
using namespace std;
#pragma comment(lib ,"SDL2.lib")
#pragma comment(lib ,"SDL2main.lib")

#define MAX_AUDIO_FARME_SIZE 48000 * 2
#define NUMBUFFERS (4)

#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)
#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)


typedef struct _tFrame {
	void* data;
	int size;
	int samplerate;
	double audio_clock;
}TFRAME, * PTFRAME;

std::queue<PTFRAME> queueData; //保存解码后数据

int thread_exit = 0;
bool thread_pause = false;
ALuint m_source;
double audio_pts;
bool seek_req = false;
double increase = 0;

int sfp_refresh_thread(int timeInterval, bool& faster, bool& sdl) {

	while (!thread_exit) {
		if (!thread_pause) {
			SDL_Event event;
			event.type = SFM_REFRESH_EVENT;
			SDL_PushEvent(&event);
			if (faster) {
				std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval)/10);
			}
			else {
				std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval));
			}
			if (sdl) {
				//SDL_Delay(20);
				std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval)*2);
			}
			else {
				std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval));
			}
			
		}
		
		//SDL_Delay(40);
	}
	//Break
	
	SDL_Event event;
	event.type = SFM_BREAK_EVENT;
	SDL_PushEvent(&event);

	return 0;
}

int sfp_control_thread(float& volumn, bool& volumnChange) {
	SDL_Event event;
	const Uint8* state = SDL_GetKeyboardState(NULL);
	bool key_space_down = false;
	bool key_plus_down = false;
	bool key_minus_down = false;
	while (!thread_exit)
	{
		if (state[SDL_SCANCODE_KP_PLUS] && !key_plus_down) {
			key_plus_down = true;
			if (volumn < 5) {
				volumn += 0.1;
			}
		}
		else if (!state[SDL_SCANCODE_KP_PLUS] && key_plus_down) {
			key_plus_down = false;
		}

		if (state[SDL_SCANCODE_KP_MINUS] && !key_minus_down) {
			key_minus_down = true;
			if (volumn > 0) {
				volumn -= 0.1;
			}
		}
		else if (!state[SDL_SCANCODE_KP_MINUS] && key_minus_down) {
			key_minus_down = false;
		}
		volumnChange = (key_plus_down || key_minus_down);

		//更新键盘状态
		state = SDL_GetKeyboardState(NULL);
	}
	return 0;
}



int SoundCallback(ALuint& bufferID) {
	if (queueData.empty()) return -1;
	PTFRAME frame = queueData.front();
	queueData.pop();
	if (frame == nullptr)
		return -1;

	alBufferData(bufferID, AL_FORMAT_STEREO16, frame->data, frame->size, frame->samplerate);
	alSourceQueueBuffers(m_source, 1, &bufferID);
	audio_pts = frame->audio_clock;

	if (frame) {
		av_free(frame->data);
		delete frame;
	}
	return 0;
}

void forward_func(double second) {
	double target_pts = audio_pts + second;

	while (!queueData.empty()) {
		PTFRAME frame = queueData.front();
		queueData.pop();
		if (frame == nullptr)
			return;
		if (frame->audio_clock >= target_pts) {
			break;
		}
		if (frame) {
			av_free(frame->data);
			delete frame;
		}
	}
}


int screen_w, screen_h;

int sdlplayer(string filePath) {
	AVFormatContext* pFormatCtx;
	int				i, videoindex;
	AVCodecContext* pCodecCtx;
	AVCodec* pCodec;
	AVFrame* pFrame, * pFrameYUV;
	unsigned char* out_buffer;
	AVPacket* packet;
	int ret, got_picture;

	//------------SDL----------------
	SDL_Window* screen;
	SDL_Renderer* sdlRenderer;
	SDL_Texture* sdlTexture;
	SDL_Rect sdlRect;
	SDL_Thread* video_tid;
	SDL_Event event;

	struct SwsContext* img_convert_ctx;
	av_register_all();
	avformat_network_init();
	pFormatCtx = avformat_alloc_context();

	if (avformat_open_input(&pFormatCtx, filePath.c_str(), NULL, NULL) != 0) {
		printf("Couldn't open input stream.\n");
		return -1;
	}
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		printf("Couldn't find stream information.\n");
		return -1;
	}
	videoindex = -1;
	for (i = 0; i < pFormatCtx->nb_streams; i++)
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoindex = i;
			break;
		}
	if (videoindex == -1) {
		printf("Didn't find a video stream.\n");
		return -1;
	}
	pCodecCtx = pFormatCtx->streams[videoindex]->codec;
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL) {
		printf("Codec not found.\n");
		return -1;
	}
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
		printf("Could not open codec.\n");
		return -1;
	}
	pFrame = av_frame_alloc();
	pFrameYUV = av_frame_alloc();

	out_buffer = (unsigned char*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1));
	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer,
		AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);

	av_dump_format(pFormatCtx, 0, filePath.c_str(), 0);

	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);


	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		printf("Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}
	screen_w = 1080; screen_h = 720;
	screen = SDL_CreateWindow("mp4player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screen_w, screen_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

	if (!screen) {
		printf("SDL: could not create window - exiting:%s\n", SDL_GetError());
		return -1;
	}
	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);

	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screen_w;
	sdlRect.h = screen_h;

	packet = (AVPacket*)av_malloc(sizeof(AVPacket));
	double frameRate = (double)pCodecCtx->framerate.num / pCodecCtx->framerate.den;
	//double frameRate = (double)pFormatCtx->streams[videoindex]->avg_frame_rate.num / pFormatCtx->streams[videoindex]->avg_frame_rate.den;
	bool faster = false;
	bool sdlfast = false;
	//cout << "pCodecCtx frameRate:" << frameRate <<endl;
	std::thread refreshThread(sfp_refresh_thread, (int)(frameRate), std::ref(faster), std::ref(sdlfast));
	refreshThread.detach();
	//------------SDL End------------
	//Event Loop
	double video_pts = 0;
	double delay = 0;
	double ptsVideo = 0;
	//audio_pts >= 0  av_read_frame(pFormatCtx, packet)>=0
	while (1) {
		while (1) {
			if (av_read_frame(pFormatCtx, packet) < 0)
				thread_exit = 1;

			if (packet->stream_index == videoindex)
				break;
		}
		ret = avcodec_send_packet(pCodecCtx, packet);
		got_picture = avcodec_receive_frame(pCodecCtx, pFrame);
		if (ret < 0) {
			cout << "Decode Error." << endl;
			return -1;
		}
		if (!got_picture) {
			sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0,
				pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
		}
		while (1) {
			SDL_WaitEvent(&event);
			if (event.type == SFM_REFRESH_EVENT) {
				if (queueData.empty()) {
					sws_freeContext(img_convert_ctx);
					SDL_Quit();
					av_frame_free(&pFrameYUV);
					av_frame_free(&pFrame);
					avcodec_close(pCodecCtx);
					avformat_close_input(&pFormatCtx);
				}
				if (true) {

					video_pts = (double)pFrame->pts * av_q2d(pFormatCtx->streams[videoindex]->time_base);
					delay = audio_pts - video_pts;
					//cout << "audio_pts - video_pts:" << delay <<"  pF-pts:"<< pFrame->pts << endl;
					if (delay > 0.03) {
						faster = true;
					}
					else if (delay < -0.03) {
						faster = false;
						sdlfast = true;
					}
					else {
						faster = false;
						sdlfast = false;
					}

				}
				SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
				SDL_RenderClear(sdlRenderer);
				SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
				SDL_RenderPresent(sdlRenderer);
				av_free_packet(packet);
				break;
			}
			else if (event.type == SDL_WINDOWEVENT)
			{
				SDL_GetWindowSize(screen, &screen_w, &screen_h);
			}
			else if (event.type == SDL_KEYDOWN) {
				switch (event.key.keysym.sym) {
				case SDLK_SPACE:
					thread_pause = thread_pause ? false : true;
					break;
				case SDLK_KP_1:
					increase = 10.0;
					cout << delay << endl;
					av_seek_frame(pFormatCtx, videoindex, (video_pts + increase + delay*frameRate)/ av_q2d(pFormatCtx->streams[videoindex]->time_base), AVSEEK_FLAG_BACKWARD);
					avcodec_flush_buffers(pCodecCtx);
					seek_req = true;
					break;
				case SDLK_KP_2:
					increase = 30.0;
					av_seek_frame(pFormatCtx, videoindex, (video_pts  + increase + delay * frameRate) / av_q2d(pFormatCtx->streams[videoindex]->time_base), AVSEEK_FLAG_BACKWARD);
					avcodec_flush_buffers(pCodecCtx);
					seek_req = true;
					
					break;
				default:
					break;
				}
			}
			else if (event.type == SDL_QUIT) {
				thread_exit = 1;
				sws_freeContext(img_convert_ctx);
				SDL_Quit();
				av_frame_free(&pFrameYUV);
				av_frame_free(&pFrame);
				avcodec_close(pCodecCtx);
				avformat_close_input(&pFormatCtx);
			}
			else if (event.type == SFM_BREAK_EVENT) {
				break;
			}
		}
		av_packet_unref(packet);
	}
	sws_freeContext(img_convert_ctx);

	SDL_Quit();
	//--------------
	av_packet_unref(packet);
	av_frame_free(&pFrameYUV);
	av_frame_free(&pFrame);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);
}

int player(string filePath) {
	AVFormatContext* pFormatCtx; //解封装
	AVCodecContext* pCodecCtx; //解码
	AVCodec* pCodec;
	AVFrame* pFrame, * pFrameYUV; //帧数据
	AVPacket* packet;	//解码前的压缩数据（包数据）
	int index; //编码器索引位置
	uint8_t* out_buffer;	//数据缓冲区
	int out_buffer_size;    //缓冲区大小
	SwrContext* swrCtx;
	double audio_clock = 0;
	av_register_all();	//注册库
	avformat_network_init();
	avcodec_register_all();
	pFormatCtx = avformat_alloc_context();

	//打开视频文件，初始化pFormatCtx
	if (avformat_open_input(&pFormatCtx, filePath.c_str(), NULL, NULL) != 0) {
		printf("Couldn't open input stream.\n");
		return -1;
	}
	//获取文件信息
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		printf("Couldn't find stream information.\n");
		return -1;
	}
	//获取各个媒体流的编码器信息，找到对应的type所在的pFormatCtx->streams的索引位置，初始化编码器。播放音频时type是AUDIO
	index = -1;
	for (int i = 0; i < pFormatCtx->nb_streams; i++)
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			index = i;
			break;
		}
	if (index == -1) {
		printf("Didn't find a video stream.\n");
		return -1;
	}
	//获取解码器
	pCodec = avcodec_find_decoder(pFormatCtx->streams[index]->codecpar->codec_id);
	if (pCodec == NULL) {
		printf("Codec not found.\n");
		return -1;
	}
	pCodecCtx = avcodec_alloc_context3(pCodec);
	avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[index]->codecpar);
	pCodecCtx->pkt_timebase = pFormatCtx->streams[index]->time_base;
	//打开解码器
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
		printf("Couldn't open codec.\n");
		return -1;
	}

	//内存分配
	packet = (AVPacket*)av_malloc(sizeof(AVPacket));
	pFrame = av_frame_alloc();
	swrCtx = swr_alloc();

	//设置采样参数 frame->16bit双声道 采样率44100 PCM采样格式   
	enum AVSampleFormat in_sample_fmt = pCodecCtx->sample_fmt;  //输入的采样格式  
	enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16; //输出采样格式16bit PCM  
	int in_sample_rate = pCodecCtx->sample_rate; //输入采样率
	int out_sample_rate = in_sample_rate; //输出采样率  
	uint64_t in_ch_layout = pCodecCtx->channel_layout; //输入的声道布局   
	uint64_t out_ch_layout = AV_CH_LAYOUT_STEREO; //输出的声道布局（立体声）
	swr_alloc_set_opts(swrCtx,
		out_ch_layout, out_sample_fmt, out_sample_rate,
		in_ch_layout, in_sample_fmt, in_sample_rate,
		0, NULL); //设置参数
	swr_init(swrCtx); //初始化

	//根据声道布局获取输出的声道个数
	int out_channel_nb = av_get_channel_layout_nb_channels(out_ch_layout);

	out_buffer = (uint8_t*)av_malloc(MAX_AUDIO_FARME_SIZE);
	int ret;
	double ptsAudio = 0;
	while (av_read_frame(pFormatCtx, packet) >= 0) {
		if (packet->stream_index == index) {
			ret = avcodec_send_packet(pCodecCtx, packet);
			if (ret < 0) {
				cout << "avcodec_send_packet：" << ret << endl;
				continue;
			}
			while (ret >= 0) {
				ret = avcodec_receive_frame(pCodecCtx, pFrame);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
					break;
				}
				else if (ret < 0) {
					cout << "avcodec_receive_frame：" << AVERROR(ret) << endl;
					return -1;
				}

				if (ret >= 0) {   //AVFrame->Audio 
					out_buffer = (uint8_t*)av_malloc(MAX_AUDIO_FARME_SIZE);
					swr_convert(swrCtx, &out_buffer, MAX_AUDIO_FARME_SIZE, (const uint8_t**)pFrame->data, pFrame->nb_samples);
					out_buffer_size = av_samples_get_buffer_size(NULL, out_channel_nb, pFrame->nb_samples, out_sample_fmt, 1);
					ptsAudio = av_q2d(pFormatCtx->streams[index]->time_base) * pFrame->pts;
					PTFRAME frame = new TFRAME;
					frame->data = out_buffer;
					frame->size = out_buffer_size;
					frame->samplerate = out_sample_rate;
					audio_clock = av_q2d(pCodecCtx->time_base) * pFrame->pts;
					frame->audio_clock = audio_clock;
					queueData.push(frame);
				}
			}
		}
		av_packet_unref(packet);
	}

	//初始化OpenAL
	ALCdevice* pDevice;
	ALCcontext* pContext;

	pDevice = alcOpenDevice(NULL);
	pContext = alcCreateContext(pDevice, NULL);
	alcMakeContextCurrent(pContext);

	if (alcGetError(pDevice) != ALC_NO_ERROR)
		return AL_FALSE;

	ALuint m_buffers[NUMBUFFERS];
	alGenSources(1, &m_source);
	if (alGetError() != AL_NO_ERROR) {
		cout << "Error generating audio source." << endl;
		return -1;
	}
	ALfloat SourcePos[] = { 0.0, 0.0, 0.0 };
	ALfloat SourceVel[] = { 0.0, 0.0, 0.0 };
	ALfloat ListenerPos[] = { 0.0, 0, 0 };
	ALfloat ListenerVel[] = { 0.0, 0.0, 0.0 };
	// first 3 elements are "at", second 3 are "up"
	ALfloat ListenerOri[] = { 0.0, 0.0, -1.0,  0.0, 1.0, 0.0 };
	//设置源属性
	alSourcef(m_source, AL_PITCH, 1.0);
	alSourcef(m_source, AL_GAIN, 1.0);
	alSourcefv(m_source, AL_POSITION, SourcePos);
	alSourcefv(m_source, AL_VELOCITY, SourceVel);
	alSourcef(m_source, AL_REFERENCE_DISTANCE, 50.0f);
	alSourcei(m_source, AL_LOOPING, AL_FALSE);
	alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);
	alListener3f(AL_POSITION, 0, 0, 0);
	alGenBuffers(NUMBUFFERS, m_buffers); //创建缓冲区

	ALint processed1 = 0;
	alGetSourcei(m_source, AL_BUFFERS_PROCESSED, &processed1);
	ALint iState;
	ALint iQueuedBuffers;
	ALint processed;

	float volumn = 1.0;
	bool volumnChange = false;
	bool fast_forward_10 = false;
	bool fast_forward_30 = false;
	std::thread controlopenal{ sfp_control_thread, std::ref(volumn), std::ref(volumnChange)};
	controlopenal.detach();

	std::thread sdlplay{ sdlplayer, filePath };
	sdlplay.detach();

	for (int i = 0; i < NUMBUFFERS; i++) {
		SoundCallback(m_buffers[i]);
	}

	alSourcePlay(m_source);
	while (!queueData.empty()) {  //队列为空后停止播放
		if (seek_req) {
			forward_func(increase);
			seek_req = false;
			increase = 0;
		}
		processed = 0;
		alGetSourcei(m_source, AL_BUFFERS_PROCESSED, &processed);
		while (processed > 0) {
			ALuint bufferID = 0;
			alSourceUnqueueBuffers(m_source, 1, &bufferID);
			SoundCallback(bufferID);
			processed--;
		}
		if (volumnChange) {
			alSourcef(m_source, AL_GAIN, volumn);
		}

		alGetSourcei(m_source, AL_SOURCE_STATE, &iState);

		if (thread_pause) {
			alSourcePause(m_source);
		}
		else if (iState != AL_PLAYING) {
			alGetSourcei(m_source, AL_BUFFERS_QUEUED, &iQueuedBuffers);
			if (iQueuedBuffers) {
				alSourcePlay(m_source);
			}
			else {
				// Finished playing
				break;
			}
		}
		if (thread_exit) {
			alSourceStop(m_source);
			alSourcei(m_source, AL_BUFFER, 0);
			alDeleteBuffers(NUMBUFFERS, m_buffers);
			alDeleteSources(1, &m_source);
			cout << "SDL closed." << endl;
			break;
		}
	}

	alSourceStop(m_source);
	alSourcei(m_source, AL_BUFFER, 0);
	alDeleteBuffers(NUMBUFFERS, m_buffers);
	alDeleteSources(1, &m_source);

	cout<<"End."<<endl;

	av_frame_free(&pFrame);
	//av_free(out_buffer);
	swr_free(&swrCtx);


	ALCcontext* pCurContext = alcGetCurrentContext();
	ALCdevice* pCurDevice = alcGetContextsDevice(pCurContext);

	alcMakeContextCurrent(NULL);
	alcDestroyContext(pCurContext);
	alcCloseDevice(pCurDevice);


	system("pause");

	return 0;
}



int main(int argc, char* argv[]) {

	if (argc != 2) {
		cout << "input error:" << endl;
		cout << "arg[1] should be the media file." << endl;
	}
	else {
		string filePath = argv[1];
		player(filePath);
	}

	return 0;
}