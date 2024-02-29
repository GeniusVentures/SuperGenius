#ifndef PROCESSING_MNN_POSENET
#define PROCESSING_MNN_POSENET
#include <math.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include <MNN/ImageProcess.hpp>
#include <MNN/Interpreter.hpp>
#define MNN_OPEN_TIME_TRACE
#include <MNN/AutoTime.hpp>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
//Defines
#define MODEL_IMAGE_SIZE 513
#define OUTPUT_STRIDE 16

#define MAX_POSE_DETECTIONS 10
#define NUM_KEYPOINTS 17
#define SCORE_THRESHOLD 0.5
#define MIN_POSE_SCORE 0.25
#define NMS_RADIUS 20
#define LOCAL_MAXIMUM_RADIUS 1

#define OFFSET_NODE_NAME "offset_2"
#define DISPLACE_FWD_NODE_NAME "displacement_fwd_2"
#define DISPLACE_BWD_NODE_NAME "displacement_bwd_2"
#define HEATMAPS "heatmap"

#define CIRCLE_RADIUS 3

namespace sgns::mnn
{
	using namespace MNN;
	class MNN_PoseNet
	{
	public:
		MNN_PoseNet(uint8_t* imagedata, 
			const char* modelfile, 
			const int origwidth, 
			const int origheight) : imageData_(imagedata), modelFile_(modelfile), originalWidth_(origwidth), originalHeight_(origheight)
		{

		}

		void StartProcessing() 
		{
			//Get Target WIdth
			const int targetWidth = static_cast<int>((float)originalWidth_ / (float)OUTPUT_STRIDE) * OUTPUT_STRIDE + 1;
			const int targetHeight = static_cast<int>((float)originalHeight_ / (float)OUTPUT_STRIDE) * OUTPUT_STRIDE + 1;

			//Scale
			CV::Point scale;
			scale.fX = (float)originalWidth_ / (float)targetWidth;
			scale.fY = (float)originalHeight_ / (float)targetHeight;

			// create net and session
			auto mnnNet = std::shared_ptr<MNN::Interpreter>(MNN::Interpreter::createFromFile(modelFile_));
			MNN::ScheduleConfig netConfig;
			netConfig.type = MNN_FORWARD_VULKAN;
			netConfig.numThread = 4;
			auto session = mnnNet->createSession(netConfig);

			auto input = mnnNet->getSessionInput(session, nullptr);

			if (input->elementSize() <= 4) {
				mnnNet->resizeTensor(input, { 1, 3, targetHeight, targetWidth });
				mnnNet->resizeSession(session);
			}
			// preprocess input image
			{
				const float means[3] = { 127.5f, 127.5f, 127.5f };
				const float norms[3] = { 2.0f / 255.0f, 2.0f / 255.0f, 2.0f / 255.0f };
				CV::ImageProcess::Config preProcessConfig;
				::memcpy(preProcessConfig.mean, means, sizeof(means));
				::memcpy(preProcessConfig.normal, norms, sizeof(norms));
				preProcessConfig.sourceFormat = CV::RGBA;
				preProcessConfig.destFormat = CV::RGB;
				preProcessConfig.filterType = CV::BILINEAR;

				auto pretreat = std::shared_ptr<CV::ImageProcess>(CV::ImageProcess::create(preProcessConfig));
				CV::Matrix trans;

				// Dst -> [0, 1]
				trans.postScale(1.0 / targetWidth, 1.0 / targetHeight);
				//[0, 1] -> Src
				trans.postScale(originalWidth_, originalHeight_);

				pretreat->setMatrix(trans);
                const auto rgbaPtr = reinterpret_cast<uint8_t*>(imageData_);
				pretreat->convert(rgbaPtr, originalWidth_, originalHeight_, 0, input);
			}
			{
				AUTOTIME;
				mnnNet->runSession(session);
			}

			// get output
			auto offsets = mnnNet->getSessionOutput(session, OFFSET_NODE_NAME);
			auto displacementFwd = mnnNet->getSessionOutput(session, DISPLACE_FWD_NODE_NAME);
			auto displacementBwd = mnnNet->getSessionOutput(session, DISPLACE_BWD_NODE_NAME);
			auto heatmaps = mnnNet->getSessionOutput(session, HEATMAPS);

			Tensor offsetsHost(offsets, Tensor::CAFFE);
			Tensor displacementFwdHost(displacementFwd, Tensor::CAFFE);
			Tensor displacementBwdHost(displacementBwd, Tensor::CAFFE);
			Tensor heatmapsHost(heatmaps, Tensor::CAFFE);

			offsets->copyToHostTensor(&offsetsHost);
			displacementFwd->copyToHostTensor(&displacementFwdHost);
			displacementBwd->copyToHostTensor(&displacementBwdHost);
			heatmaps->copyToHostTensor(&heatmapsHost);

			std::vector<float> poseScores;
			std::vector<std::vector<float>> poseKeypointScores;
			std::vector<std::vector<CV::Point>> poseKeypointCoords;
			{
				AUTOTIME;
				decodeMultiPose(&offsetsHost, &displacementFwdHost, &displacementBwdHost, &heatmapsHost, poseScores,
					poseKeypointScores, poseKeypointCoords, scale);
			}

			drawPose(imageData_, originalWidth_, originalHeight_, poseScores, poseKeypointScores, poseKeypointCoords);
			stbi_write_png("outputImageFileName.png", originalWidth_, originalHeight_, 4, imageData_, 4 * originalWidth_);
			stbi_image_free(imageData_);
		}
	private:
		static int changeColorCircle(uint32_t* src, CV::Point point, int width, int height);
		static int drawPose(uint8_t* rgbaPtr, int width, int height, std::vector<float>& poseScores,
			std::vector<std::vector<float>>& poseKeypointScores,
			std::vector<std::vector<CV::Point>>& poseKeypointCoords);
		static CV::Point getCoordsFromTensor(const Tensor* dataTensor, int id, int x, int y, bool getCoord = true);
		static int decodePoseImpl(float curScore, int curId, const CV::Point& originalOnImageCoords, const Tensor* heatmaps,
			const Tensor* offsets, const Tensor* displacementFwd, const Tensor* displacementBwd,
			std::vector<float>& instanceKeypointScores, std::vector<CV::Point>& instanceKeypointCoords);
		static int decodeMultiPose(const Tensor* offsets, const Tensor* displacementFwd, const Tensor* displacementBwd,
			const Tensor* heatmaps, std::vector<float>& poseScores,
			std::vector<std::vector<float>>& poseKeypointScores,
			std::vector<std::vector<CV::Point>>& poseKeypointCoords, CV::Point& scale);

		uint8_t* imageData_;
		const char* modelFile_;
		int originalWidth_;
		int originalHeight_;

	};


	//Pose Model Names
	const std::vector<std::string> PoseNames{ "nose",         "leftEye",       "rightEye",  "leftEar",    "rightEar",
										 "leftShoulder", "rightShoulder", "leftElbow", "rightElbow", "leftWrist",
										 "rightWrist",   "leftHip",       "rightHip",  "leftKnee",   "rightKnee",
										 "leftAnkle",    "rightAnkle" };

	const std::vector<std::pair<std::string, std::string>> PoseChain{
		{"nose", "leftEye"},          {"leftEye", "leftEar"},        {"nose", "rightEye"},
		{"rightEye", "rightEar"},     {"nose", "leftShoulder"},      {"leftShoulder", "leftElbow"},
		{"leftElbow", "leftWrist"},   {"leftShoulder", "leftHip"},   {"leftHip", "leftKnee"},
		{"leftKnee", "leftAnkle"},    {"nose", "rightShoulder"},     {"rightShoulder", "rightElbow"},
		{"rightElbow", "rightWrist"}, {"rightShoulder", "rightHip"}, {"rightHip", "rightKnee"},
		{"rightKnee", "rightAnkle"} };
}

#endif