#include "../src/randomPatten.hpp";
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/calib3d.hpp"
#include <vector>
#include <iostream>
#include <time.h>
using namespace std;
using namespace cv;

const char * usage = 
    "\n example command line for calibrate a camera by random pattern. \n"
    "   randomPatternCalibration -pw 20.3 -ph 28.5 -nf 500 -mm 20 image_list.xml \n"
    "\n"
    " the file image_list.xml is generated by imagelist_creator as\n"
    "imagelist_creator image_list.xml *.*";
static void help()
{
    printf("\n This is a sample for camera calibration by a random pattern.\n"
           "Usage: randomPatternCalibration\n"
           "    -pw <pattern_width> # the physical width of random pattern\n"
           "    -ph <pattern_height> # the physical height of random pattern\n"
           "    -nf <number_feature> # number of feature points to extract\n"
           "    -mm <minimal_match> # minimal number of matches\n"
           "    [-fp ] # fix the principal point at the center \n"
           "    input_data # input data - text file with a list of the images of the board, which is generated by imagelist_creator"
           );
    printf("\n %s", usage);
}

static bool readStringList( const string& filename, vector<string>& l )
{
    l.resize(0);
    FileStorage fs(filename, FileStorage::READ);
    if( !fs.isOpened() )
        return false;
    FileNode n = fs.getFirstTopLevelNode();
    if( n.type() != FileNode::SEQ )
        return false;
    FileNodeIterator it = n.begin(), it_end = n.end();
    for( ; it != it_end; ++it )
        l.push_back((string)*it);
    return true;
}

static void saveCameraParams(const string& filename, Size imageSize, float patternWidth,
    float patternHeight, int flags, const Mat& cameraMatrix, const Mat& distCoeffs,
    const vector<Mat>& rvecs, const vector<Mat>& tvecs, double rms)
{
    FileStorage fs (filename, FileStorage::WRITE );
    time_t tt;
    time( &tt );
    struct tm *t2 = localtime( &tt );
    char buf[1024];
    strftime( buf, sizeof(buf)-1, "%c", t2 );

    fs << "calibration_time" << buf;

    if( !rvecs.empty())
        fs << "nframes" << (int)rvecs.size();

    fs << "image_width" << imageSize.width;
    fs << "image_height" << imageSize.height;
    fs << "pattern_width" << patternWidth;
    fs << "pattern_height" << patternHeight;

    fs << "flags" <<flags;

    fs << "camera_matrix" << cameraMatrix;
    fs << "distortion_coefficients" << distCoeffs;

    fs << "rms" << rms;

    if( !rvecs.empty() && !tvecs.empty() )
    {
        CV_Assert(rvecs[0].type() == tvecs[0].type());
        Mat bigmat((int)rvecs.size(), 6, rvecs[0].type());
        for( int i = 0; i < (int)rvecs.size(); i++ )
        {
            Mat r = bigmat(Range(i, i+1), Range(0,3));
            Mat t = bigmat(Range(i, i+1), Range(3,6));

            CV_Assert(rvecs[i].rows == 3 && rvecs[i].cols == 1);
            CV_Assert(tvecs[i].rows == 3 && tvecs[i].cols == 1);
            //*.t() is MatExpr (not Mat) so we can use assignment operator
            r = rvecs[i].t();
            t = tvecs[i].t();
        }
        //cvWriteComment( *fs, "a set of 6-tuples (rotation vector + translation vector) for each view", 0 );
        fs << "extrinsic_parameters" << bigmat;
    }
}

int main(int argc, char** argv)
{
    const char* inputFilename;
    const char* outputFilename = "out_camera_params.xml";
    vector<string> imglist;
    vector<Mat> vecImg;
    int flags = 0;
    float patternWidth, patternHeight;
    int nFatures, nMiniMatches;
    if(argc < 2)
    {
        help();
        return 1;
    }

    for (int i = 1; i < argc; ++i)
    {
        const char* s = argv[i];
        if(strcmp(s, "-pw") == 0)
        {
            if(sscanf(argv[++i], "%f", &patternWidth) != 1 || patternWidth <= 0)
                return fprintf( stderr, "Invalid pattern width\n"), -1;
        }
        else if(strcmp(s, "-ph") == 0)
        {
            if(sscanf(argv[++i], "%f", &patternHeight) != 1 || patternHeight <= 0)
                return fprintf( stderr, "Invalid pattern height\n"), -1;
        }
        else if (strcmp(s, "-nf") == 0)
        {
            if (sscanf(argv[++i], "%d", & nFatures) != 1 || nFatures <= 50)
                return fprintf( stderr, "Invalid number of features or number is too small"), -1;
        }
        else if (strcmp(s, "-mm") == 0)
        {
            if (sscanf(argv[++i], "%d", &nMiniMatches) != 1 || nMiniMatches < 5)
                return fprintf( stderr, "Invalid number of minimal matches or number is too small"), -1;
        }
        else if( strcmp( s, "-fp" ) == 0 )
        {
            flags |= CALIB_FIX_PRINCIPAL_POINT;
        }
        else if( s[0] != '-')
        {
            inputFilename = s;
        }
        else
        {
            return fprintf( stderr, "Unknown option %s\n", s ), -1;
        }
    }

    readStringList(inputFilename, imglist);
    // the first image is the pattern
    Mat pattern = cv::imread(imglist[0], cv::IMREAD_GRAYSCALE);
    for (int i = 1; i < (int)imglist.size(); ++i)
    {
        Mat img;
        img = cv::imread(imglist[i], cv::IMREAD_GRAYSCALE);
        vecImg.push_back(img);
    }

    randomPatternCornerFinder finder(patternWidth, patternHeight, nFatures, nMiniMatches, CV_32F);
    finder.computeObjectImagePoints(vecImg, pattern);
    vector<Mat> objectPoints = finder.getObjectPoints();
    vector<Mat> imagePoints = finder.getImagePoints();
    
    Mat K;
    Mat D;
    vector<Mat> rvec, tvec;
    double rms = calibrateCamera(objectPoints, imagePoints, vecImg[0].size(), K, D, rvec, tvec);
    saveCameraParams("camera_params.xml", vecImg[0].size(), 20.3, 28.5, flags, K, D, rvec, tvec, rms);
}