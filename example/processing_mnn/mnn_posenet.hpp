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
		MNN_PoseNet(std::vector<uint8_t>* imagedata,
			const char* modelfile, 
			const int origwidth, 
			const int origheight) : imageData_(imagedata), modelFile_(modelfile), originalWidth_(origwidth), originalHeight_(origheight)
		{

		}
		~MNN_PoseNet()
		{
			stbi_image_free(imageData_);
		}
		std::vector<uint8_t> StartProcessing();
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

		std::vector<uint8_t>* imageData_;
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