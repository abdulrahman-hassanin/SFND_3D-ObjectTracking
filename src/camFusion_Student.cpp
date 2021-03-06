
#include <iostream>
#include <algorithm>
#include <numeric>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "camFusion.hpp"
#include "dataStructures.h"

using namespace std;


// Create groups of Lidar points whose projection into the camera falls into the same bounding box
void clusterLidarWithROI(std::vector<BoundingBox> &boundingBoxes, std::vector<LidarPoint> &lidarPoints, float shrinkFactor, cv::Mat &P_rect_xx, cv::Mat &R_rect_xx, cv::Mat &RT)
{
    // loop over all Lidar points and associate them to a 2D bounding box
    cv::Mat X(4, 1, cv::DataType<double>::type);
    cv::Mat Y(3, 1, cv::DataType<double>::type);

    for (auto it1 = lidarPoints.begin(); it1 != lidarPoints.end(); ++it1)
    {
        // assemble vector for matrix-vector-multiplication
        X.at<double>(0, 0) = it1->x;
        X.at<double>(1, 0) = it1->y;
        X.at<double>(2, 0) = it1->z;
        X.at<double>(3, 0) = 1;

        // project Lidar point into camera
        Y = P_rect_xx * R_rect_xx * RT * X;
        cv::Point pt;
        pt.x = Y.at<double>(0, 0) / Y.at<double>(0, 2); // pixel coordinates
        pt.y = Y.at<double>(1, 0) / Y.at<double>(0, 2);

        vector<vector<BoundingBox>::iterator> enclosingBoxes; // pointers to all bounding boxes which enclose the current Lidar point
        for (vector<BoundingBox>::iterator it2 = boundingBoxes.begin(); it2 != boundingBoxes.end(); ++it2)
        {
            // shrink current bounding box slightly to avoid having too many outlier points around the edges
            cv::Rect smallerBox;
            smallerBox.x = (*it2).roi.x + shrinkFactor * (*it2).roi.width / 2.0;
            smallerBox.y = (*it2).roi.y + shrinkFactor * (*it2).roi.height / 2.0;
            smallerBox.width = (*it2).roi.width * (1 - shrinkFactor);
            smallerBox.height = (*it2).roi.height * (1 - shrinkFactor);

            // check wether point is within current bounding box
            if (smallerBox.contains(pt))
            {
                enclosingBoxes.push_back(it2);
            }

        } // eof loop over all bounding boxes

        // check wether point has been enclosed by one or by multiple boxes
        if (enclosingBoxes.size() == 1)
        { 
            // add Lidar point to bounding box
            enclosingBoxes[0]->lidarPoints.push_back(*it1);
        }

    } // eof loop over all Lidar points
}


void show3DObjects(std::vector<BoundingBox> &boundingBoxes, cv::Size worldSize, cv::Size imageSize, bool bWait)
{
    // create topview image
    cv::Mat topviewImg(imageSize, CV_8UC3, cv::Scalar(255, 255, 255));

    for(auto it1=boundingBoxes.begin(); it1!=boundingBoxes.end(); ++it1)
    {
        // create randomized color for current 3D object
        cv::RNG rng(it1->boxID);
        cv::Scalar currColor = cv::Scalar(rng.uniform(0,150), rng.uniform(0, 150), rng.uniform(0, 150));

        // plot Lidar points into top view image
        int top=1e8, left=1e8, bottom=0.0, right=0.0; 
        float xwmin=1e8, ywmin=1e8, ywmax=-1e8;
        for (auto it2 = it1->lidarPoints.begin(); it2 != it1->lidarPoints.end(); ++it2)
        {
            // world coordinates
            float xw = (*it2).x; // world position in m with x facing forward from sensor
            float yw = (*it2).y; // world position in m with y facing left from sensor
            xwmin = xwmin<xw ? xwmin : xw;
            ywmin = ywmin<yw ? ywmin : yw;
            ywmax = ywmax>yw ? ywmax : yw;

            // top-view coordinates
            int y = (-xw * imageSize.height / worldSize.height) + imageSize.height;
            int x = (-yw * imageSize.width / worldSize.width) + imageSize.width / 2;

            // find enclosing rectangle
            top = top<y ? top : y;
            left = left<x ? left : x;
            bottom = bottom>y ? bottom : y;
            right = right>x ? right : x;

            // draw individual point
            cv::circle(topviewImg, cv::Point(x, y), 4, currColor, -1);
        }

        // draw enclosing rectangle
        cv::rectangle(topviewImg, cv::Point(left, top), cv::Point(right, bottom),cv::Scalar(0,0,0), 2);

        // augment object with some key data
        char str1[200], str2[200];
        sprintf(str1, "id=%d, #pts=%d", it1->boxID, (int)it1->lidarPoints.size());
        putText(topviewImg, str1, cv::Point2f(left-250, bottom+50), cv::FONT_ITALIC, 2, currColor);
        sprintf(str2, "xmin=%2.2f m, yw=%2.2f m", xwmin, ywmax-ywmin);
        putText(topviewImg, str2, cv::Point2f(left-250, bottom+125), cv::FONT_ITALIC, 2, currColor);  
    }

    // plot distance markers
    float lineSpacing = 2.0; // gap between distance markers
    int nMarkers = floor(worldSize.height / lineSpacing);
    for (size_t i = 0; i < nMarkers; ++i)
    {
        int y = (-(i * lineSpacing) * imageSize.height / worldSize.height) + imageSize.height;
        cv::line(topviewImg, cv::Point(0, y), cv::Point(imageSize.width, y), cv::Scalar(255, 0, 0));
    }

    // display image
    string windowName = "3D Objects";
    cv::namedWindow(windowName, 1);
    cv::imshow(windowName, topviewImg);

    if(bWait)
    {
        cv::waitKey(0); // wait for key to be pressed
    }
}


// associate a given bounding box with the keypoints it contains
void clusterKptMatchesWithROI(BoundingBox &boundingBox, std::vector<cv::KeyPoint> &kptsPrev, std::vector<cv::KeyPoint> &kptsCurr, std::vector<cv::DMatch> &kptMatches)
{
	vector<double> distances;
    double mean_dist = 0;
    double dist =0;
    for (cv::DMatch m : kptMatches) {
    	cv::KeyPoint pPrev = kptsPrev[m.queryIdx];
    	cv::KeyPoint pCurr = kptsCurr[m.trainIdx];

        dist = sqrt(pow(pPrev.pt.x - pCurr.pt.x, 2) + pow(pPrev.pt.y - pCurr.pt.y, 2));
        mean_dist += dist;
    	distances.push_back(dist);
    }

    mean_dist /= distances.size();
    double stdev = 0.0;
    for (double d : distances) {
    	stdev += pow(d - mean_dist, 2);
    }
    stdev = sqrt(stdev / distances.size());

    for (int i = 0; i < kptMatches.size(); i++) {
    	if (abs(distances[i] - mean_dist) < stdev) {
    		boundingBox.kptMatches.push_back(kptMatches[i]);
    	}
    }
}

double getMedian(std::vector<double> v)
{
    double median = 0.0;
    int size = v.size();

    sort(v.begin(), v.end());

    if(size%2 == 0)
        median = (v[size/2] + v[size/2 -1]) / 2;
    else
        median = v[size/2];

    return median;

}

// Compute time-to-collision (TTC) based on keypoint correspondences in successive images
void computeTTCCamera(std::vector<cv::KeyPoint> &kptsPrev, std::vector<cv::KeyPoint> &kptsCurr, 
                      std::vector<cv::DMatch> kptMatches, double frameRate, double &TTC, cv::Mat *visImg)
{
    // compute distance ratios between all matched keypoints
    vector<double> distRatios; // stores the distance ratios for all keypoints between curr. and prev. frame
    for (auto it1 = kptMatches.begin(); it1 != kptMatches.end() - 1; ++it1)
    { // outer kpt. loop

        // get current keypoint and its matched partner in the prev. frame
        cv::KeyPoint kpOuterCurr = kptsCurr.at(it1->trainIdx);
        cv::KeyPoint kpOuterPrev = kptsPrev.at(it1->queryIdx);

        for (auto it2 = kptMatches.begin() + 1; it2 != kptMatches.end(); ++it2)
        { // inner kpt.-loop

            double minDist = 100.0; // min. required distance

            // get next keypoint and its matched partner in the prev. frame
            cv::KeyPoint kpInnerCurr = kptsCurr.at(it2->trainIdx);
            cv::KeyPoint kpInnerPrev = kptsPrev.at(it2->queryIdx);

            // compute distances and distance ratios
            double distCurr = cv::norm(kpOuterCurr.pt - kpInnerCurr.pt);
            double distPrev = cv::norm(kpOuterPrev.pt - kpInnerPrev.pt);

            if (distPrev > std::numeric_limits<double>::epsilon() && distCurr >= minDist)
            { // avoid division by zero

                double distRatio = distCurr / distPrev;
                distRatios.push_back(distRatio);
            }
        } // eof inner loop over all matched kpts
    }     // eof outer loop over all matched kpts

    // only continue if list of distance ratios is not empty
    if (distRatios.size() == 0)
    {
        TTC = NAN;
        return;
    }

    // compute camera-based TTC from distance ratios
    double meanDistRatio = std::accumulate(distRatios.begin(), distRatios.end(), 0.0) / distRatios.size();

    double dT = 1 / frameRate;
    TTC = -dT / (1 - meanDistRatio);

    // STUDENT TASK (replacement for meanDistRatio)
    std::sort(distRatios.begin(), distRatios.end());
  	int median = floor(distRatios.size() / 2.0);
  	double medianDistRatios;	
  
 	if( distRatios.size() % 2 == 0)
      medianDistRatios = ( distRatios[median-1] + distRatios[median] )/2.0;
  	else
      medianDistRatios = distRatios[median];
  
  	double dt = 1 / frameRate;
  	TTC = -dt / (1 - medianDistRatios);
}

void computeTTCLidar(std::vector<LidarPoint> &lidarPointsPrev,
                     std::vector<LidarPoint> &lidarPointsCurr, double frameRate, double &TTC)
{
    // ...
    // take x-distance medin in each frame
    vector<double> dPrev, dCurr;

    for(LidarPoint p : lidarPointsPrev)
        dPrev.push_back(p.x);
    for(LsidarPoint p : lidarPointsCurr)
        dCurr.push_back(p.x);

    double d0, d1, dt;
    d0 = getMedian(dPrev);
    d1 = getMedian(dCurr);
    dt = 1/frameRate;

    TTC = d1 * dt / (d0 - d1);
}

/*void matchBoundingBoxes(std::vector<cv::DMatch> &matches, std::map<int, int> &bbBestMatches, DataFrame &prevFrame, DataFrame &currFrame)
{
    std::multimap<int, int> mmap;
    int maxPrevBoxID = 0;
    // Dmatch contains two keypoint indices, queryIdx and trainIdx base in the oreder of image argument to match
    for(auto match : matches){
        cv::KeyPoint prevKP = prevFrame.keypoints[match.queryIdx];
        cv::KeyPoint currKP = currFrame.keypoints[match.trainIdx];

        int prevBoxID, currBoxID = -1;

        // find it in each bounding box in prev frame
        for(auto bBox : prevFrame.boundingBoxes){
            if(bBox.roi.contains(prevKP.pt)){
                prevBoxID = bBox.boxID;
            }    
        }
        // find it in each bounding box in curr frame
        for(auto bBox : currFrame.boundingBoxes){
            if(bBox.roi.contains(currKP.pt)){
                currBoxID = bBox.boxID;
            }
        }
        mmap.insert({prevBoxID, currBoxID});
        maxPrevBoxID = std::max(maxPrevBoxID, prevBoxID);
    }

    // Setup a list of boxID int values to iterate over in the current frame
    vector<int> currFrameBoxIDs {};
    for (auto box : currFrame.boundingBoxes) 
        currFrameBoxIDs.push_back(box.boxID);

    // Loop through each boxID in the current frame, and get the mode (most frequent value) of associated boxID for the previous frame.
    for (int k : currFrameBoxIDs) {
        // Count the greatest number of matches in the multimap, where each element is {key=currBoxID, val=prevBoxID}
        // std::multimap::equal_range(k) returns the range of all elements matching key = k.
        auto rangePrevBoxIDs = mmap.equal_range(k);

        // Create a vector of counts (per current bbox) of prevBoxIDs
        std::vector<int> counts(maxPrevBoxID + 1, 0);

        // Accumulator loop
        for (auto it = rangePrevBoxIDs.first; it != rangePrevBoxIDs.second; ++it) {
            if (-1 != (*it).second) 
                counts[(*it).second] += 1;
        }

        // Get the index of the maximum count (the mode) of the previous frame's boxID
        int modeIndex = std::distance(counts.begin(), std::max_element(counts.begin(), counts.end()));

        // Set the best matching bounding box map with
        // key   = Previous frame's most likely matching boxID
        // value = Current frame's boxID, k
        bbBestMatches.insert({modeIndex, k});
    }


}*/

void matchBoundingBoxes(std::vector<cv::DMatch> &matches, std::map<int, int> &bbBestMatches, DataFrame &prevFrame, DataFrame &currFrame)
{
	// bbBestMatches - prevFrame to currFrame
	map<int, map<int, int>> counts;
    for (cv::DMatch match : matches) {
    	cv::KeyPoint query = prevFrame.keypoints[match.queryIdx];
    	bool query_found = false;
    	int query_pos, train_pos;
    	cv::KeyPoint train = currFrame.keypoints[match.trainIdx];
    	bool train_found = false;
    	for (int i = 0; i < prevFrame.boundingBoxes.size(); i++) {
    		if (prevFrame.boundingBoxes[i].roi.contains(cv::Point(query.pt.x, query.pt.y))) {
    			query_found = true;
    			query_pos = i;
    			break;
    		}
    	}
    	for (int i = 0; i < currFrame.boundingBoxes.size(); i++) {
    		if (currFrame.boundingBoxes[i].roi.contains(cv::Point(train.pt.x, train.pt.y))) {
    			train_found = true;
    			train_pos = i;
    			break;
    		}
    	}
    	if (query_found && train_found) {
    		if (counts.count(query_pos) == 1) {
    			counts[query_pos][train_pos] += 1;
    		} else {
    			counts[query_pos][train_pos] = 1;
    		}
    	}
    }
    for (auto c : counts) {
    	int max = 0;
    	int value = -1;
    	for (auto m : c.second) {
    		if (m.second > max) {
    			max = m.second;
    			value = m.first;
    		}
    	}
    	if (value != -1) {
    		bbBestMatches[c.first] = value;
    	}
    }
}
