#pragma once
#include <math.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include <MNN/ImageProcess.hpp>
#include <MNN/Interpreter.hpp>
#include <processing/processing_processor.hpp>
#define MNN_OPEN_TIME_TRACE
#include <MNN/AutoTime.hpp>

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

namespace sgns::processing
{
	using namespace MNN;


	class MNN_PoseNet : public ProcessingProcessor
	{
	public:
		MNN_PoseNet() : imageData_(std::make_unique<std::vector<std::vector<char>>>()), modelFile_(std::make_unique<std::vector<uint8_t>>()) {}

		~MNN_PoseNet()
		{
			//stbi_image_free(imageData_);
		};
        std::vector<uint8_t> StartProcessing(SGProcessing::SubTaskResult& result, const SGProcessing::Task& task, const SGProcessing::SubTask& subTask) override;

        void SetData(std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers) override;


	private:
        std::vector<uint8_t> MNNProcess(std::vector<uint8_t>* imgdata, const int origwidth,
            const int origheight);


        int changeColorCircle(uint32_t* src, CV::Point point, int width, int height);
        int drawPose(uint8_t* rgbaPtr, int width, int height, std::vector<float>& poseScores,
            std::vector<std::vector<float>>& poseKeypointScores,
            std::vector<std::vector<CV::Point>>& poseKeypointCoords);
        CV::Point getCoordsFromTensor(const Tensor* dataTensor, int id, int x, int y, bool getCoord = true);
        int decodePoseImpl(float curScore, int curId, const CV::Point& originalOnImageCoords, const Tensor* heatmaps,
            const Tensor* offsets, const Tensor* displacementFwd, const Tensor* displacementBwd,
            std::vector<float>& instanceKeypointScores, std::vector<CV::Point>& instanceKeypointCoords);
        int decodeMultiPose(const Tensor* offsets, const Tensor* displacementFwd, const Tensor* displacementBwd,
            const Tensor* heatmaps, std::vector<float>& poseScores,
            std::vector<std::vector<float>>& poseKeypointScores,
            std::vector<std::vector<CV::Point>>& poseKeypointCoords, CV::Point& scale);
        float clip(float value, float min, float max);


		std::unique_ptr<std::vector<std::vector<char>>> imageData_;
        std::unique_ptr<std::vector<uint8_t>> modelFile_;
		std::string fileName_;

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
	};

}
