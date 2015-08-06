#include "precomp.hpp"
#include "multiCameraCalibration.hpp"
#include<string>
#include <vector>
#include<queue>
#include <iostream>
using namespace cv;

multiCameraCalibration::multiCameraCalibration(int cameraType, int nCameras, const std::string& fileName,
    float patternWidth, float patternHeight, int showExtration, int nMiniMatches, TermCriteria criteria,
    Ptr<FeatureDetector> detector, Ptr<DescriptorExtractor> descriptor,
    Ptr<DescriptorMatcher> matcher)
{
    _camType = cameraType;
    _nCamera = nCameras;
    _nMiniMatches = nMiniMatches;
    _filename = fileName;
    _patternWidth = patternWidth;
    _patternHeight = patternHeight;
    _criteria = criteria;
    _showExtraction = showExtration;
    _objectPointsForEachCamera.resize(_nCamera);
    _imagePointsForEachCamera.resize(_nCamera);
    _cameraMatrix.resize(_nCamera);
    _distortCoeffs.resize(_nCamera);
    _xi.resize(_nCamera);
    _omEachCamera.resize(_nCamera);
    _tEachCamera.resize(_nCamera);
    _detector = detector;
    _descriptor = descriptor;
    _matcher = matcher;

    for (int i = 0; i < _nCamera; ++i)
    {
        _vertexList.push_back(vertex());
    }
}

double multiCameraCalibration::run()
{
    loadImages();
    initialize();
    double error = optimizeExtrinsics();
    return error;
}

std::vector<std::string> multiCameraCalibration::readStringList()
{
    std::vector<std::string> l;
    l.resize(0);
    FileStorage fs(_filename, FileStorage::READ);
    
    FileNode n = fs.getFirstTopLevelNode();
        
    FileNodeIterator it = n.begin(), it_end = n.end();
    for( ; it != it_end; ++it )
        l.push_back((std::string)*it);

    return l;
}

void multiCameraCalibration::loadImages()
{
    std::vector<std::string> file_list;
    file_list = readStringList();

    //Ptr<FeatureDetector> detector = AKAZE::create(AKAZE::DESCRIPTOR_MLDB,
    //    0, 3, 0.002f);
    //Ptr<DescriptorExtractor> descriptor = AKAZE::create(AKAZE::DESCRIPTOR_MLDB,
    //    0, 3, 0.002f);
    //Ptr<DescriptorMatcher> matcher = DescriptorMatcher::create("BruteForce-L1");
    Ptr<FeatureDetector> detector = _detector;
    Ptr<DescriptorExtractor> descriptor = _descriptor;
    Ptr<DescriptorMatcher> matcher = _matcher;

    randomPatternCornerFinder finder(_patternWidth, _patternHeight, 10, CV_32F, this->_showExtraction, detector, descriptor, matcher);
    Mat pattern = cv::imread(file_list[0]);
    finder.loadPattern(pattern);

    std::vector<std::vector<std::string> > filesEachCameraFull(_nCamera);
    std::vector<std::vector<int> > timestampFull(_nCamera);

    for (int i = 1; i < (int)file_list.size(); ++i)
    {
        int cameraVertex, timestamp;
        std::string filename = file_list[i].substr(0, file_list[i].find('.'));
        int spritPosition1 = filename.rfind('/');
        int spritPosition2 = filename.rfind('\\');
        if (spritPosition1!=std::string::npos)
        {
            filename = filename.substr(spritPosition1+1, filename.size() - 1);
        }
        else if(spritPosition2!= std::string::npos)
        {
            filename = filename.substr(spritPosition2+1, filename.size() - 1);
        }
        sscanf(filename.c_str(), "%d-%d", &cameraVertex, &timestamp);
        filesEachCameraFull[cameraVertex].push_back(file_list[i]);
        timestampFull[cameraVertex].push_back(timestamp);
    }
    

    // calibrate each camera individually
    for (int camera = 0; camera < _nCamera; ++camera)
    {
        Mat image, cameraMatrix, distortCoeffs;

        // find image and object points
        for (int imgIdx = 0; imgIdx < (int)filesEachCameraFull[camera].size(); ++imgIdx)
        {
            image = imread(filesEachCameraFull[camera][imgIdx], IMREAD_GRAYSCALE);
            std::vector<Mat> imgObj = finder.computeObjectImagePointsForSingle(image);
            _imagePointsForEachCamera[camera].push_back(imgObj[0]);
            _objectPointsForEachCamera[camera].push_back(imgObj[1]);
        }

        // calibrate
        Mat idx;
        double rms;
        if (_camType == PINHOLE)
        {
            rms = cv::calibrateCamera(_objectPointsForEachCamera[camera], _imagePointsForEachCamera[camera],
                image.size(), _cameraMatrix[camera], _distortCoeffs[camera], _omEachCamera[camera],
                _tEachCamera[camera]);
            idx = Mat(1, (int)_omEachCamera[camera].size(), CV_32S);
            for (int i = 0; i < (int)idx.total(); ++i)
            {
                idx.at<int>(i) = i;
            }
        }
        else if (_camType == OMNIDIRECTIONAL)
        {
            rms = cv::omnidir::calibrate(_objectPointsForEachCamera[camera], _imagePointsForEachCamera[camera],
                image.size(), _cameraMatrix[camera], _xi[camera], _distortCoeffs[camera], _omEachCamera[camera],
                _tEachCamera[camera], 0, TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 200, 1e-7),
                idx);
        }
        _cameraMatrix[camera].convertTo(_cameraMatrix[camera], CV_32F);
        _distortCoeffs[camera].convertTo(_distortCoeffs[camera], CV_32F);
        //else
        //{
        //    CV_Error_(CV_StsOutOfRange, "Unknown camera type, use PINHOLE or OMNIDIRECTIONAL");
        //}
        
        for (int i = 0; i < (int)_omEachCamera[camera].size(); ++i)
        {
            if ((int)_objectPointsForEachCamera[camera][idx.at<int>(i)].total() > _nMiniMatches)
            {
                int cameraVertex, timestamp, photoVertex;
                cameraVertex = camera;
                timestamp = timestampFull[camera][idx.at<int>(i)];

                photoVertex = this->getPhotoVertex(timestamp);

                if (_omEachCamera[camera][i].type()!=CV_32F)
                {
                    _omEachCamera[camera][i].convertTo(_omEachCamera[camera][i], CV_32F);
                }
                if (_tEachCamera[camera][i].type()!=CV_32F)
                {
                    _tEachCamera[camera][i].convertTo(_tEachCamera[camera][i], CV_32F);
                }

                Mat transform = Mat::eye(4, 4, CV_32F);
                Mat R, T;
                Rodrigues(_omEachCamera[camera][i], R);
                T = (_tEachCamera[camera][i]).reshape(1, 3);
                R.copyTo(transform.rowRange(0, 3).colRange(0, 3));
                T.copyTo(transform.rowRange(0, 3).col(3));

                this->_edgeList.push_back(edge(cameraVertex, photoVertex, idx.at<int>(i), transform));
            }
        }
    }

}

int multiCameraCalibration::getPhotoVertex(int timestamp)
{
    int photoVertex = INVALID;

    // find in existing photo vertex
    for (int i = 0; i < (int)_vertexList.size(); ++i)
    {
        if (_vertexList[i].timestamp == timestamp)
        {
            photoVertex = i;
            break;
        }
    }

    // add a new photo vertex
    if (photoVertex == INVALID)
    {
        _vertexList.push_back(vertex(Mat::eye(4, 4, CV_32F), timestamp));
        photoVertex = (int)_vertexList.size() - 1;
    }

    return photoVertex;
}

void multiCameraCalibration::initialize()
{
    int nVertices = (int)_vertexList.size();
    int nEdges = (int) _edgeList.size();
    
    // build graph
    Mat G = Mat::zeros((int)this->_vertexList.size(), (int)this->_vertexList.size(), CV_32S);
    for (int edgeIdx = 0; edgeIdx < (int)this->_edgeList.size(); ++edgeIdx)
    {
        G.at<int>(this->_edgeList[edgeIdx].cameraVertex, this->_edgeList[edgeIdx].photoVertex) = edgeIdx + 1;
    }
    G = G + G.t();

    // traverse the graph
    std::vector<int> pre, order;
    graphTraverse(G, 0, order, pre);

    for (int i = 0; i < _nCamera; ++i)
    {
        if (pre[i] == INVALID)
        {
            std::cout << "camera" << i << "is not connected" << std::endl;
        }
    }

    for (int i = 1; i < (int)order.size(); ++i)
    {
        int vertexIdx = order[i];
        Mat prePose = this->_vertexList[pre[vertexIdx]].pose;
        int edgeIdx = G.at<int>(vertexIdx, pre[vertexIdx]) - 1;
        Mat transform = this->_edgeList[edgeIdx].transform;

        if (vertexIdx < _nCamera)
        {
            this->_vertexList[vertexIdx].pose = transform * prePose.inv();
            this->_vertexList[vertexIdx].pose.convertTo(this->_vertexList[vertexIdx].pose, CV_32F);
        }
        else
        {
            this->_vertexList[vertexIdx].pose = prePose.inv() * transform;
            this->_vertexList[vertexIdx].pose.convertTo(this->_vertexList[vertexIdx].pose, CV_32F);
        }
    }
}

double multiCameraCalibration::optimizeExtrinsics()
{
    // get om, t vector
    int nVertex = (int)this->_vertexList.size();
    
    Mat extrinParam(1, (nVertex-1)*6, CV_32F);
    int offset = 0;
    // the pose of the vertex[0] is eye
    for (int i = 1; i < nVertex; ++i)
    {
        Mat rvec, tvec;
        cv::Rodrigues(this->_vertexList[i].pose.rowRange(0,3).colRange(0, 3), rvec);
        this->_vertexList[i].pose.rowRange(0,3).col(3).copyTo(tvec);

        rvec.reshape(1, 1).copyTo(extrinParam.colRange(offset, offset + 3));
        tvec.reshape(1, 1).copyTo(extrinParam.colRange(offset+3, offset +6));
        offset += 6;
    }
    double error_pre = computeProjectError(extrinParam);
    // optimization
    const double alpha_smooth = 0.01;
    //const double thresh_cond = 1e6;
    double change = 1;
    for(int iter = 0; ; ++iter)
    {
        if ((_criteria.type == 1 && iter >= _criteria.maxCount)  ||
            (_criteria.type == 2 && change <= _criteria.epsilon) ||
            (_criteria.type == 3 && (change <= _criteria.epsilon || iter >= _criteria.maxCount)))
            break;
        double alpha_smooth2 = 1 - std::pow(1 - alpha_smooth, (double)iter + 1.0);
        Mat JTJ_inv, JTError;
        this->computeJacobianExtrinsic(extrinParam, JTJ_inv, JTError);
        Mat G = alpha_smooth2*JTJ_inv * JTError;
        if (G.depth() == CV_64F)
        {
            G.convertTo(G, CV_32F);
        }
        
        extrinParam = extrinParam + G.reshape(1, 1);
        change = norm(G) / norm(extrinParam);
        double error = computeProjectError(extrinParam);
    }

    double error = computeProjectError(extrinParam);

    std::vector<Vec3f> rvecVertex, tvecVertex;
    vector2parameters(extrinParam, rvecVertex, tvecVertex);
    for (int verIdx = 1; verIdx < (int)_vertexList.size(); ++verIdx)
    {
        Mat R;
        Mat pose = Mat::eye(4, 4, CV_32F);
        Rodrigues(rvecVertex[verIdx-1], R);
        R.copyTo(pose.colRange(0, 3).rowRange(0, 3));
        Mat(tvecVertex[verIdx-1]).reshape(1, 3).copyTo(pose.rowRange(0, 3).col(3));
        _vertexList[verIdx].pose = pose;
    }
    return error;
}

void multiCameraCalibration::computeJacobianExtrinsic(const Mat& extrinsicParams, Mat& JTJ_inv, Mat& JTE)
{
    int nParam = (int)extrinsicParams.total();
    int nEdge = (int)_edgeList.size();
    std::vector<int> pointsLocation(nEdge+1, 0);

    for (int edgeIdx = 0; edgeIdx < nEdge; ++edgeIdx)
    {
        int nPoints = _objectPointsForEachCamera[_edgeList[edgeIdx].cameraVertex][_edgeList[edgeIdx].photoIndex].total();
        pointsLocation[edgeIdx+1] = pointsLocation[edgeIdx] + nPoints*2;
    }

    JTJ_inv = Mat(nParam, nParam, CV_64F);
    JTE = Mat(nParam, 1, CV_64F);

    Mat J = Mat::zeros(pointsLocation[nEdge], nParam, CV_64F);
    Mat E = Mat::zeros(pointsLocation[nEdge], 1, CV_64F);

    for (int edgeIdx = 0; edgeIdx < nEdge; ++edgeIdx)
    {
        int photoVertex = _edgeList[edgeIdx].photoVertex;
        int photoIndex = _edgeList[edgeIdx].photoIndex;
        int cameraVertex = _edgeList[edgeIdx].cameraVertex;

        Mat objectPoints = _objectPointsForEachCamera[cameraVertex][photoIndex];
        Mat imagePoints = _imagePointsForEachCamera[cameraVertex][photoIndex];

        Mat rvecTran, tvecTran;
        Mat R = _edgeList[edgeIdx].transform.rowRange(0, 3).colRange(0, 3);
        tvecTran = _edgeList[edgeIdx].transform.rowRange(0, 3).col(3);
        cv::Rodrigues(R, rvecTran);
        
        Mat rvecPhoto = extrinsicParams.colRange((photoVertex-1)*6, (photoVertex-1)*6 + 3);
        Mat tvecPhoto = extrinsicParams.colRange((photoVertex-1)*6 + 3, (photoVertex-1)*6 + 6);

        Mat rvecCamera, tvecCamera;
        if (cameraVertex > 0)
        {
            rvecCamera = extrinsicParams.colRange((cameraVertex-1)*6, (cameraVertex-1)*6 + 3);
            tvecCamera = extrinsicParams.colRange((cameraVertex-1)*6 + 3, (cameraVertex-1)*6 + 6);
        }
        else
        {
            rvecCamera = Mat::zeros(3, 1, CV_32F);
            tvecCamera = Mat::zeros(3, 1, CV_32F);
        }

        Mat jacobianPhoto, jacobianCamera, error;
        computePhotoCameraJacobian(rvecPhoto, tvecPhoto, rvecCamera, tvecCamera, rvecTran, tvecTran,
            objectPoints, imagePoints, this->_cameraMatrix[cameraVertex], this->_distortCoeffs[cameraVertex],
            this->_xi[cameraVertex], jacobianPhoto, jacobianCamera, error);
        if (cameraVertex > 0)
        {
            jacobianCamera.copyTo(J.rowRange(pointsLocation[edgeIdx], pointsLocation[edgeIdx+1]).
                colRange((cameraVertex-1)*6, cameraVertex*6));
        }
        jacobianPhoto.copyTo(J.rowRange(pointsLocation[edgeIdx], pointsLocation[edgeIdx+1]).
            colRange((photoVertex-1)*6, photoVertex*6));
        error.copyTo(E.rowRange(pointsLocation[edgeIdx], pointsLocation[edgeIdx+1]));
    }
    //std::cout << J.t() * J << std::endl;
    JTJ_inv = (J.t() * J + 1e-10).inv();
    JTE = J.t() * E;

}
void multiCameraCalibration::computePhotoCameraJacobian(const Mat& rvecPhoto, const Mat& tvecPhoto, const Mat& rvecCamera,
    const Mat& tvecCamera, Mat& rvecTran, Mat& tvecTran, const Mat& objectPoints, const Mat& imagePoints, const Mat& K,
    const Mat& distort, const Mat& xi, Mat& jacobianPhoto, Mat& jacobianCamera, Mat& E)
{
    Mat drvecTran_drecvPhoto, drvecTran_dtvecPhoto,
        drvecTran_drvecCamera, drvecTran_dtvecCamera,
        dtvecTran_drvecPhoto, dtvecTran_dtvecPhoto,
        dtvecTran_drvecCamera, dtvecTran_dtvecCamera;

    multiCameraCalibration::compose_motion(rvecPhoto, tvecPhoto, rvecCamera, tvecCamera, rvecTran, tvecTran,
        drvecTran_drecvPhoto, drvecTran_dtvecPhoto, drvecTran_drvecCamera, drvecTran_dtvecCamera,
        dtvecTran_drvecPhoto, dtvecTran_dtvecPhoto, dtvecTran_drvecCamera, dtvecTran_dtvecCamera);

    if (rvecTran.depth() == CV_64F)
    {
        rvecTran.convertTo(rvecTran, CV_32F);
    }
    if (tvecTran.depth() == CV_64F)
    {
        tvecTran.convertTo(tvecTran, CV_32F);
    }
    float _xi;
    if (_camType == OMNIDIRECTIONAL)
    {
        _xi= xi.at<float>(0);
    }
    
    Mat imagePoints2, jacobian, dx_drvecCamera, dx_dtvecCamera, dx_drvecPhoto, dx_dtvecPhoto;
    if (_camType == PINHOLE)
    {
        cv::projectPoints(objectPoints, rvecTran, tvecTran, K, distort, imagePoints2, jacobian);
    }
    else if (_camType == OMNIDIRECTIONAL)
    {
        cv::omnidir::projectPoints(objectPoints, imagePoints2, rvecTran, tvecTran, K, _xi, distort, jacobian);    
    }
    if (objectPoints.depth() == CV_32F)
    {
        Mat(imagePoints - imagePoints2).convertTo(E, CV_64FC2);
    }
    else
    {
        E = imagePoints - imagePoints2;
    }
    E = E.reshape(1, (int)imagePoints.total()*2);

    dx_drvecCamera = jacobian.colRange(0, 3) * drvecTran_drvecCamera + jacobian.colRange(3, 6) * dtvecTran_drvecCamera;
    dx_dtvecCamera = jacobian.colRange(0, 3) * drvecTran_dtvecCamera + jacobian.colRange(3, 6) * dtvecTran_dtvecCamera;
    dx_drvecPhoto = jacobian.colRange(0, 3) * drvecTran_drecvPhoto + jacobian.colRange(3, 6) * dtvecTran_drvecPhoto;
    dx_dtvecPhoto = jacobian.colRange(0, 3) * drvecTran_dtvecPhoto + jacobian.colRange(3, 6) * dtvecTran_dtvecPhoto;

    jacobianCamera = cv::Mat(dx_drvecCamera.rows, 6, CV_64F);
    jacobianPhoto = cv::Mat(dx_drvecPhoto.rows, 6, CV_64F);

    dx_drvecCamera.copyTo(jacobianCamera.colRange(0, 3));
    dx_dtvecCamera.copyTo(jacobianCamera.colRange(3, 6));
    dx_drvecPhoto.copyTo(jacobianPhoto.colRange(0, 3));
    dx_dtvecPhoto.copyTo(jacobianPhoto.colRange(3, 6));

}
void multiCameraCalibration::graphTraverse(const Mat& G, int begin, std::vector<int>& order, std::vector<int>& pre)
{
    CV_Assert(!G.empty() && G.rows == G.cols);
    int nVertex = G.rows;
    order.resize(0);
    pre.resize(nVertex, INVALID);
    pre[begin] = -1;
    std::vector<bool> visited(nVertex, false);
    std::queue<int> q;
    visited[begin] = true;
    q.push(begin);
    order.push_back(begin);

    while(!q.empty())
    {
        int v = q.front();
        q.pop();
        Mat idx;
        // use my findNonZero maybe
        findRowNonZero(G.row(v), idx);
        for(int i = 0; i < (int)idx.total(); ++i)
        {
            int neighbor = idx.at<int>(i);
            if (!visited[neighbor])
            {
                visited[neighbor] = true;
                q.push(neighbor);
                order.push_back(neighbor);
                pre[neighbor] = v;
            }
        }
    }
}

void multiCameraCalibration::findRowNonZero(const Mat& row, Mat& idx)
{
    CV_Assert(!row.empty() && row.rows == 1 && row.channels() == 1);
    Mat _row;
    std::vector<int> _idx;
    row.convertTo(_row, CV_32F);
    for (int i = 0; i < (int)row.total(); ++i)
    {
        if (_row.at<float>(i) != 0)
        {
            _idx.push_back(i);
        }
    }
    idx.release();
    idx.create(1, (int)_idx.size(), CV_32S);
    for (int i = 0; i < (int)_idx.size(); ++i)
    {
        idx.at<int>(i) = _idx[i];
    }
}

double multiCameraCalibration::computeProjectError(Mat& parameters)
{
    int nVertex = (int)_vertexList.size();
    CV_Assert(parameters.total() == (nVertex-1) * 6 && parameters.depth() == CV_32F);
    int nEdge = (int)_edgeList.size();

    // recompute the transform between photos and cameras

    std::vector<edge> edgeList = this->_edgeList;
    std::vector<Vec3f> rvecVertex, tvecVertex;
    vector2parameters(parameters, rvecVertex, tvecVertex);

    float totalError = 0;
    int totalNPoints = 0;
    for (int edgeIdx = 0; edgeIdx < nEdge; ++edgeIdx)
    {
        Mat RPhoto, RCamera, TPhoto, TCamera, transform;
        int cameraVertex = edgeList[edgeIdx].cameraVertex;
        int photoVertex = edgeList[edgeIdx].photoVertex;
        int PhotoIndex = edgeList[edgeIdx].photoIndex;
        TPhoto = Mat(tvecVertex[photoVertex - 1]).reshape(1, 3);
        
        //edgeList[edgeIdx].transform = Mat::ones(4, 4, CV_32F);
        transform = Mat::eye(4, 4, CV_32F);
        cv::Rodrigues(rvecVertex[photoVertex-1], RPhoto);
        if (cameraVertex == 0)
        {
            //RPhoto.copyTo(edgeList[edgeIdx].transform.rowRange(0, 3).colRange(0, 3));
            RPhoto.copyTo(transform.rowRange(0, 3).colRange(0, 3));
            TPhoto.copyTo(transform.rowRange(0, 3).col(3));
            //TPhoto.copyTo(transform.rowRange(0, 3).col(3));
        }
        else
        {
            TCamera = Mat(tvecVertex[cameraVertex - 1]).reshape(1, 3);
            cv::Rodrigues(rvecVertex[cameraVertex - 1], RCamera);
            //Mat(RCamera*RPhoto).copyTo(edgeList[edgeIdx].transform.rowRange(0, 3).colRange(0, 3));
            Mat(RCamera*RPhoto).copyTo(transform.rowRange(0, 3).colRange(0, 3));
            //Mat(RCamera * TPhoto + TCamera).copyTo(edgeList[edgeIdx].transform.rowRange(0, 3).col(3));
            Mat(RCamera * TPhoto + TCamera).copyTo(transform.rowRange(0, 3).col(3));
        }

        transform.copyTo(edgeList[edgeIdx].transform);
        Mat rvec, tvec;
        cv::Rodrigues(transform.rowRange(0, 3).colRange(0, 3), rvec);
        transform.rowRange(0, 3).col(3).copyTo(tvec);

        Mat objectPoints, imagePoints, proImagePoints;
        objectPoints = this->_objectPointsForEachCamera[cameraVertex][PhotoIndex];
        imagePoints = this->_imagePointsForEachCamera[cameraVertex][PhotoIndex];

        if (this->_camType == PINHOLE)
        {
            cv::projectPoints(objectPoints, rvec, tvec, _cameraMatrix[cameraVertex], _distortCoeffs[cameraVertex],
                proImagePoints);
        }
        else if (this->_camType == OMNIDIRECTIONAL)
        {
            float xi = _xi[cameraVertex].at<float>(0);
            
            cv::omnidir::projectPoints(objectPoints, proImagePoints, rvec, tvec, _cameraMatrix[cameraVertex],
                xi, _distortCoeffs[cameraVertex]);
        }
        Mat error = imagePoints - proImagePoints;
        Vec2f* ptr_err = error.ptr<Vec2f>();
        for (int i = 0; i < (int)error.total(); ++i)
        {
            totalError += sqrt(ptr_err[i][0]*ptr_err[i][0] + ptr_err[i][1]*ptr_err[i][1]);
        }
        totalNPoints += (int)error.total();
    }
    double meanReProjError = totalError / totalNPoints;
    return meanReProjError;
}

void multiCameraCalibration::compose_motion(InputArray _om1, InputArray _T1, InputArray _om2, InputArray _T2, Mat& om3, Mat& T3, Mat& dom3dom1,
    Mat& dom3dT1, Mat& dom3dom2, Mat& dom3dT2, Mat& dT3dom1, Mat& dT3dT1, Mat& dT3dom2, Mat& dT3dT2)
{
    Mat om1, om2, T1, T2;
    _om1.getMat().convertTo(om1, CV_64F);
    _om2.getMat().convertTo(om2, CV_64F);
    _T1.getMat().reshape(1, 3).convertTo(T1, CV_64F);
    _T2.getMat().reshape(1, 3).convertTo(T2, CV_64F);
    /*Mat om2 = _om2.getMat();
    Mat T1 = _T1.getMat().reshape(1, 3);
    Mat T2 = _T2.getMat().reshape(1, 3);*/

    //% Rotations:
    Mat R1, R2, R3, dR1dom1(9, 3, CV_64FC1), dR2dom2;
    cv::Rodrigues(om1, R1, dR1dom1);
    cv::Rodrigues(om2, R2, dR2dom2);
    /*JRodriguesMatlab(dR1dom1, dR1dom1);
    JRodriguesMatlab(dR2dom2, dR2dom2);*/
    dR1dom1 = dR1dom1.t();
    dR2dom2 = dR2dom2.t();

    R3 = R2 * R1;
    Mat dR3dR2, dR3dR1;
    //dAB(R2, R1, dR3dR2, dR3dR1);
    matMulDeriv(R2, R1, dR3dR2, dR3dR1);
    Mat dom3dR3;
    cv::Rodrigues(R3, om3, dom3dR3);
    //JRodriguesMatlab(dom3dR3, dom3dR3);
    dom3dR3 = dom3dR3.t();

    dom3dom1 = dom3dR3 * dR3dR1 * dR1dom1;
    dom3dom2 = dom3dR3 * dR3dR2 * dR2dom2;
    dom3dT1 = Mat::zeros(3, 3, CV_64FC1);
    dom3dT2 = Mat::zeros(3, 3, CV_64FC1);

    //% Translations:
    Mat T3t = R2 * T1;
    Mat dT3tdR2, dT3tdT1;
    //dAB(R2, T1, dT3tdR2, dT3tdT1);
    matMulDeriv(R2, T1, dT3tdR2, dT3tdT1);

    Mat dT3tdom2 = dT3tdR2 * dR2dom2;
    T3 = T3t + T2;
    dT3dT1 = dT3tdT1;
    dT3dT2 = Mat::eye(3, 3, CV_64FC1);
    dT3dom2 = dT3tdom2;
    dT3dom1 = Mat::zeros(3, 3, CV_64FC1);
}

void multiCameraCalibration::JRodriguesMatlab(const Mat& src, Mat& dst)
{
    Mat tmp(src.cols, src.rows, src.type());
    if (src.rows == 9)
    {
        Mat(src.row(0).t()).copyTo(tmp.col(0));
        Mat(src.row(1).t()).copyTo(tmp.col(3));
        Mat(src.row(2).t()).copyTo(tmp.col(6));
        Mat(src.row(3).t()).copyTo(tmp.col(1));
        Mat(src.row(4).t()).copyTo(tmp.col(4));
        Mat(src.row(5).t()).copyTo(tmp.col(7));
        Mat(src.row(6).t()).copyTo(tmp.col(2));
        Mat(src.row(7).t()).copyTo(tmp.col(5));
        Mat(src.row(8).t()).copyTo(tmp.col(8));
    }
    else
    {
        Mat(src.col(0).t()).copyTo(tmp.row(0));
        Mat(src.col(1).t()).copyTo(tmp.row(3));
        Mat(src.col(2).t()).copyTo(tmp.row(6));
        Mat(src.col(3).t()).copyTo(tmp.row(1));
        Mat(src.col(4).t()).copyTo(tmp.row(4));
        Mat(src.col(5).t()).copyTo(tmp.row(7));
        Mat(src.col(6).t()).copyTo(tmp.row(2));
        Mat(src.col(7).t()).copyTo(tmp.row(5));
        Mat(src.col(8).t()).copyTo(tmp.row(8));
    }
    dst = tmp.clone();
}

void multiCameraCalibration::dAB(InputArray A, InputArray B, OutputArray dABdA, OutputArray dABdB)
{
    CV_Assert(A.getMat().cols == B.getMat().rows);
    CV_Assert(A.type() == CV_64FC1 && B.type() == CV_64FC1);

    int p = A.getMat().rows;
    int n = A.getMat().cols;
    int q = B.getMat().cols;

    dABdA.create(p * q, p * n, CV_64FC1);
    dABdB.create(p * q, q * n, CV_64FC1);

    dABdA.getMat() = Mat::zeros(p * q, p * n, CV_64FC1);
    dABdB.getMat() = Mat::zeros(p * q, q * n, CV_64FC1);

    for (int i = 0; i < q; ++i)
    {
        for (int j = 0; j < p; ++j)
        {
            int ij = j + i * p;
            for (int k = 0; k < n; ++k)
            {
                int kj = j + k * p;
                dABdA.getMat().at<double>(ij, kj) = B.getMat().at<double>(k, i);
            }
        }
    }

    for (int i = 0; i < q; ++i)
    {
        A.getMat().copyTo(dABdB.getMat().rowRange(i * p, i * p + p).colRange(i * n, i * n + n));
    }
}

void multiCameraCalibration::vector2parameters(const Mat& parameters, std::vector<Vec3f>& rvecVertex, std::vector<Vec3f>& tvecVertexs)
{
    int nVertex = (int)_vertexList.size();
    CV_Assert(parameters.channels() == 1 && parameters.total() == 6*(nVertex - 1));
    CV_Assert(parameters.depth() == CV_32F);
    parameters.reshape(1, 1);

    rvecVertex.reserve(0);
    tvecVertexs.resize(0);

    for (int i = 0; i < nVertex - 1; ++i)
    {
        rvecVertex.push_back(Vec3f(parameters.colRange(i*6, i*6 + 3)));
        tvecVertexs.push_back(Vec3f(parameters.colRange(i*6 + 3, i*6 + 6)));
    }
}

void multiCameraCalibration::parameters2vector(const std::vector<Vec3f>& rvecVertex, const std::vector<Vec3f>& tvecVertex, Mat& parameters)
{
    CV_Assert(rvecVertex.size() == tvecVertex.size());
    int nVertex = (int)rvecVertex.size();
    // the pose of the first camera is known
    parameters.create(1, 6*(nVertex-1), CV_32F);

    for (int i = 0; i < nVertex-1; ++i)
    {
        Mat(rvecVertex[i]).reshape(1, 1).copyTo(parameters.colRange(i*6, i*6 + 3));
        Mat(tvecVertex[i]).reshape(1, 1).copyTo(parameters.colRange(i*6 + 3, i*6 + 6));
    }
}

void multiCameraCalibration::writeParameters(const std::string& filename)
{
    FileStorage fs( filename, FileStorage::WRITE );

    fs << "nCameras" << _nCamera;

    for (int camIdx = 0; camIdx < _nCamera; ++camIdx)
    {
        char num[10];
        sprintf(num, "%d", camIdx);
        std::string cameraMatrix = "camera_matrix_" + std::string(num);
        std::string cameraPose = "camera_pose_" + std::string(num);
        std::string cameraDistortion = "camera_distortion_" + std::string(num);
        std::string cameraXi = "xi_" + std::string(num);

        fs << cameraMatrix << _cameraMatrix[camIdx];
        fs << cameraDistortion << _distortCoeffs[camIdx];
        if (_camType == OMNIDIRECTIONAL)
        {
            fs << cameraXi << _xi[camIdx].at<float>(0);
        }
        
        fs << cameraPose << _vertexList[camIdx].pose;
    }

    for (int photoIdx = _nCamera; photoIdx < (int)_vertexList.size(); ++photoIdx)
    {
        char timestamp[100];
        sprintf(timestamp, "%d", _vertexList[photoIdx].timestamp);
        std::string photoTimestamp = "pose_timestamp_" + std::string(timestamp);

        fs << photoTimestamp << _vertexList[photoIdx].pose;
    }
}