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
int thread_exit = 0;
int thread_pause = 0;
int sfp_refresh_thread(int timeInterval,bool &faster, bool& sdl) {
	thread_exit = 0;
	thread_pause = 0;

	while (!thread_exit) {
		if (!thread_pause) {
			SDL_Event event;
			event.type = SFM_REFRESH_EVENT;
			SDL_PushEvent(&event);
		}
		if (faster) {
			std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval)/2);
		}
		else {
			std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval));
		}
		if (sdl) {
			SDL_Delay(20);
			//std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval)*2);
		}
		//SDL_Delay(40);
	}
	thread_exit = 0;
	thread_pause = 0;
	//Break
	SDL_Event event;
	event.type = SFM_BREAK_EVENT;
	SDL_PushEvent(&event);

	return 0;
}

typedef struct _tFrame {
	void* data;
	int size;
	int samplerate;
	double audio_clock;
	int64_t audio_timestamp;
}TFRAME, * PTFRAME;

std::queue<PTFRAME> queueData; //保存解码后数据
ALuint m_source;
double audio_pts;
int64_t audio_timestamp;
int SoundCallback(ALuint& bufferID) {
	if (queueData.empty()) return -1;
	PTFRAME frame = queueData.front();
	queueData.pop();
	if (frame == nullptr)
		return -1;
	//把数据写入buffer
	alBufferData(bufferID, AL_FORMAT_STEREO16, frame->data, frame->size, frame->samplerate);
	//将buffer放回缓冲区
	alSourceQueueBuffers(m_source, 1, &bufferID);
	audio_pts = frame->audio_clock;
	//cout << "frame->audio_clock:" << frame->audio_clock << endl;

	//释放数据
	if (frame) {
		av_free(frame->data);
		delete frame;
	}
	return 0;
}

int Play() {
	int state;
	alGetSourcei(m_source, AL_SOURCE_STATE, &state);
	if (state == AL_STOPPED || state == AL_INITIAL) {
		alSourcePlay(m_source);
	}
	return 0;
}



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
	int screen_w, screen_h;
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

	//Output Info-----------------------------
	printf("---------------- File Information ---------------\n");
	av_dump_format(pFormatCtx, 0, filePath.c_str(), 0);
	printf("-------------------------------------------------\n");

	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);


	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		printf("Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}
	//SDL 2.0 Support for multiple windows
	//screen_w = pCodecCtx->width;
	//screen_h = pCodecCtx->height;
	screen_w = 1080; screen_h = 720;
	screen = SDL_CreateWindow("mp4player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screen_w, screen_h, SDL_WINDOW_OPENGL);

	if (!screen) {
		printf("SDL: could not create window - exiting:%s\n", SDL_GetError());
		return -1;
	}
	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	//IYUV: Y + U + V  (3 planes)
	//YV12: Y + V + U  (3 planes)
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);

	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screen_w;
	sdlRect.h = screen_h;

	packet = (AVPacket*)av_malloc(sizeof(AVPacket));
	double frameRate = (double)pCodecCtx->framerate.num / pCodecCtx->framerate.den;
	//
	//double frameRate = (double)pFormatCtx->streams[videoindex]->avg_frame_rate.num / pFormatCtx->streams[videoindex]->avg_frame_rate.den;
	bool faster = false;
	bool sdlfast = false;
	//cout << "pCodecCtx frameRate:" << frameRate <<endl;
	std::thread refreshThread(sfp_refresh_thread, (int)(frameRate), std::ref(faster), std::ref(sdlfast));
	//------------SDL End------------
	//Event Loop
	double video_pts = 0;
	double delay = 0;
	while (audio_pts >= 0) {
		//Wait
		SDL_WaitEvent(&event);
		if (event.type == SFM_REFRESH_EVENT) {
			while (1) {
				if (av_read_frame(pFormatCtx, packet) < 0)
					thread_exit = 1;

				if (packet->stream_index == videoindex)
					break;
			}
			ret = avcodec_send_packet(pCodecCtx, packet);
			got_picture = avcodec_receive_frame(pCodecCtx, pFrame);
			if (ret < 0) {
				cout<<"Decode Error."<<endl;
				return -1;
			}
			if (!got_picture) {
				sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0,
					pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
			}
			while(1){
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
						//cout << "video_pts:"<<video_pts<<endl;
						delay = audio_pts - video_pts;
						if (delay > 0.03) {
							//cout << "audio_pts - video_pts:" << delay << endl;
							faster = true;
						}
						else if(delay <-0.03) {
							//cout << "video_pts - audio_pts:" << (video_pts - audio_pts)<< endl;
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
					//SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );  
					SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
					SDL_RenderPresent(sdlRenderer);
					av_free_packet(packet);
					break;
				}
				else if (event.type == SDL_KEYDOWN) {
					//Pause
					if (event.key.keysym.sym == SDLK_SPACE)
						thread_pause = !thread_pause;
				}
				else if (event.type == SDL_QUIT) {
					thread_exit = 1;
				}
				else if (event.type == SFM_BREAK_EVENT) {
					break;
				}
			}
			
		}
		

	}
	sws_freeContext(img_convert_ctx);

	SDL_Quit();
	//--------------
	av_frame_free(&pFrameYUV);
	av_frame_free(&pFrame);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);
}

int player(string filePath) {
	/*char* filepath;
	if (argc == 2) {
		filepath = argv[1];
	}
	else {
		printf("Usage: audioPlayer <filepath>");
		return -1;
	}*/

	//char filepath[] = "test.mp4";
	//string filePath = "F:/forgit/gitrep/mp4player/mp4player/42stest.mp4";

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
					//重采样
					swr_convert(swrCtx, &out_buffer, MAX_AUDIO_FARME_SIZE, (const uint8_t**)pFrame->data, pFrame->nb_samples);
					//获取有多少有效的数据在out_buffer的内存上
					out_buffer_size = av_samples_get_buffer_size(NULL, out_channel_nb, pFrame->nb_samples, out_sample_fmt, 1);
					PTFRAME frame = new TFRAME;
					frame->data = out_buffer;
					frame->size = out_buffer_size;
					frame->samplerate = out_sample_rate;
					audio_clock = av_q2d(pCodecCtx->time_base) * pFrame->pts;
					frame->audio_clock = audio_clock;
					//cout<<"frame->size:"<< pFrame->channels<<"  frame->samplerate:"<< frame->samplerate <<endl;
					//audio_clock += static_cast<double>(frame->size) / out_sample_rate;
					//audio_clock += out_buffer_size/ out_sample_rate;
					//cout <<"audio_clock:" << audio_clock << endl;
					queueData.push(frame);  //解码后数据存入队列
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

	std::thread sdlplay{ sdlplayer, filePath };
	sdlplay.detach();

	for (int i = 0; i < NUMBUFFERS; i++) {
		SoundCallback(m_buffers[i]);
	}
	alSourcePlay(m_source);
	while (!queueData.empty()) {  //队列为空后停止播放
		ALint processed = 0;
		alGetSourcei(m_source, AL_BUFFERS_PROCESSED, &processed);
		while (processed > 0) {
			ALuint bufferID = 0;
			alSourceUnqueueBuffers(m_source, 1, &bufferID);
			SoundCallback(bufferID);
			processed--;
		}
		Play();
	}

	alSourceStop(m_source);
	alSourcei(m_source, AL_BUFFER, 0);
	alDeleteBuffers(NUMBUFFERS, m_buffers);
	alDeleteSources(1, &m_source);

	printf("End.\n");

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