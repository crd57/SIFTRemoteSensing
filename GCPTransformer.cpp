#include "GCPTransformer.h"

int CreateGCPsList(vector<KeyPoint> keypoints_1, vector<KeyPoint> keypoints_2, vector<DMatch> Matches, ImageInfo info_1, ImageInfo info_2, GDAL_GCP* gcplist)
{
	vector<vector<float>> GCPPixel;
	vector<vector<float>> GCPXY;
	float x_error = 0, y_error = 0;
	float XRMSE = 0,YRMSE = 0;

	for (int i = 0; i < Matches.size(); i++)
	{
		Point2f pt_1;
		Point2f pt_2;
		float x_1,x_2, y_1,y_2;
		pt_1 = keypoints_1[Matches[i].queryIdx].pt;
		ImageRowCol2Projection(info_1, pt_1.x, pt_1.y, x_1, y_1);
		pt_2 = keypoints_2[Matches[i].trainIdx].pt;
		ImageRowCol2Projection(info_2, pt_2.x, pt_2.y, x_2, y_2);
		x_error += (x_1 - x_2) * (x_1 - x_2);
		y_error += (y_1 - y_2) * (y_1 - y_2);
		char id[10];
		sprintf(id, "%d", i);
		gcplist[i].pszId = id;
		gcplist[i].pszInfo = (char*) "create by SIFT";
		gcplist[i].dfGCPLine = (double)pt_2.y;
		gcplist[i].dfGCPPixel = (double)pt_2.x;
		gcplist[i].dfGCPX = (double)x_1;
		gcplist[i].dfGCPY = (double)y_1;
		gcplist[i].dfGCPZ = 0;
	}
	XRMSE = x_error / Matches.size();
	YRMSE = y_error / Matches.size();
	cout << "匹配前 X 方向均方根跟误差" << XRMSE << endl;
	cout << "匹配前 Y 方向均方根跟误差" << YRMSE << endl;
	return Matches.size();
}

int ImageWarpByGCP(const char * pszSrcFile,
	const char * pszDstFile,
	int nGCPCount,
	const GDAL_GCP *pasGCPList,
	const char * pszDstWKT,
	int iOrder,
	double dResX,
	double dResY,
	GDALResampleAlg eResampleMethod,
	const char * pszFormat)
{

	GDALAllRegister();
	CPLSetConfigOption("GDAL_FILENAME_IS_UTF8", "NO");

	// 打开原始图像
	GDALDatasetH hSrcDS = GDALOpen(pszSrcFile, GA_ReadOnly);
	if (NULL == hSrcDS)
	{
		return 0;
	}

	GDALDataType eDataType = GDALGetRasterDataType(GDALGetRasterBand(hSrcDS, 1));
	int nBandCount = GDALGetRasterCount(hSrcDS);

	// 创建几何多项式坐标转换关系
	void *hTransform = GDALCreateGCPTransformer(nGCPCount, pasGCPList, iOrder, FALSE);
	float* RMSE = CheckGCPTransform(hTransform, nGCPCount, pasGCPList);

	if (NULL == hTransform)
	{
		GDALClose(hSrcDS);
		return 0;
	}
	// 计算输出图像四至范围、大小、仿射变换六参数等信息
	double adfGeoTransform[6] = { 0 };
	double adfExtent[4] = { 0 };
	int    nPixels = 0, nLines = 0;

	if (GDALSuggestedWarpOutput2(hSrcDS, GDALGCPTransform, hTransform,
		adfGeoTransform, &nPixels, &nLines, adfExtent, 0) != CE_None)
	{
		GDALClose(hSrcDS);
		return 0;
	}

	// 下面开始根据用户指定的分辨率来反算输出图像的大小和六参数等信息
	double dResXSize = dResX;
	double dResYSize = dResY;

	//如果为0，则默认为原始影像的分辨率
	if (dResXSize == 0.0 && dResYSize == 0.0)
	{
		double dbGeoTran[6] = { 0 };
		GDALGCPsToGeoTransform(nGCPCount, pasGCPList, dbGeoTran, 0);
		dResXSize = fabs(dbGeoTran[1]);
		dResYSize = fabs(dbGeoTran[5]);
	}

	// 如果用户指定了输出图像的分辨率
	else if (dResXSize != 0.0 || dResYSize != 0.0)
	{
		if (dResXSize == 0.0) dResXSize = adfGeoTransform[1];
		if (dResYSize == 0.0) dResYSize = adfGeoTransform[5];
	}

	if (dResXSize < 0.0) dResXSize = -dResXSize;
	if (dResYSize > 0.0) dResYSize = -dResYSize;

	// 计算输出图像的范围
	double minX = adfGeoTransform[0];
	double maxX = adfGeoTransform[0] + adfGeoTransform[1] * nPixels;
	double maxY = adfGeoTransform[3];
	double minY = adfGeoTransform[3] + adfGeoTransform[5] * nLines;

	nPixels = ceil((maxX - minX) / dResXSize);
	nLines = ceil((minY - maxY) / dResYSize);
	adfGeoTransform[0] = minX;
	adfGeoTransform[3] = maxY;
	adfGeoTransform[1] = dResXSize;
	adfGeoTransform[5] = dResYSize;

	// 创建输出图像
	GDALDriverH hDriver = GDALGetDriverByName(pszFormat);
	if (NULL == hDriver)
	{
		return 0;
	}
	GDALDatasetH hDstDS = GDALCreate(hDriver, pszDstFile, nPixels, nLines, nBandCount, eDataType, NULL);
	if (NULL == hDstDS)
	{
		return 0;
	}
	GDALSetProjection(hDstDS, pszDstWKT);
	GDALSetGeoTransform(hDstDS, adfGeoTransform);

	//获得原始图像的行数和列数
	int nXsize = GDALGetRasterXSize(hSrcDS);
	int nYsize = GDALGetRasterYSize(hSrcDS);

	//然后是图像重采样
	int nFlag = 0;
	float dfValue = 0;
	CPLErr err = CE_Failure;


	//最邻近采样
	for (int nBandIndex = 0; nBandIndex < nBandCount; nBandIndex++)
	{
		GDALRasterBandH hSrcBand = GDALGetRasterBand(hSrcDS, nBandIndex + 1);
		GDALRasterBandH hDstBand = GDALGetRasterBand(hDstDS, nBandIndex + 1);
		for (int nRow = 0; nRow < nLines; nRow++)
		{
			for (int nCol = 0; nCol < nPixels; nCol++)
			{
				double dbX = adfGeoTransform[0] + nCol * adfGeoTransform[1]
					+ nRow * adfGeoTransform[2];
				double dbY = adfGeoTransform[3] + nCol * adfGeoTransform[4]
					+ nRow * adfGeoTransform[5];

				//由输出的图像地理坐标系变换到原始的像素坐标系
				GDALGCPTransform(hTransform, TRUE, 1, &dbX, &dbY, NULL, &nFlag);
				int nXCol = (int)(dbX + 0.5);
				int nYRow = (int)(dbY + 0.5);

				//超出范围的用0填充
				if (nXCol < 0 || nXCol >= nXsize || nYRow < 0 || nYRow >= nYsize)
				{
					dfValue = 0;
				}

				else
				{
					err = GDALRasterIO(hSrcBand, GF_Read, nXCol, nYRow, 1, 1, &dfValue, 1, 1, eDataType, 0, 0);

				}
				err = GDALRasterIO(hDstBand, GF_Write, nCol, nRow, 1, 1, &dfValue, 1, 1, eDataType, 0, 0);

			}
		}

	}

	if (hTransform != NULL)
	{
		GDALDestroyGCPTransformer(hTransform);
		hTransform = NULL;
	}

	GDALClose(hSrcDS);
	GDALClose(hDstDS);


	return 1;
}

float* CheckGCPTransform(void* GCPTransform, int nGCPCount, const GDAL_GCP* pasGCPList)
{
	double* x = new double[nGCPCount];
	double* y = new double[nGCPCount];
	double* z = new double[nGCPCount];
	int* su = new int[nGCPCount];
	float x_error = 0, y_error = 0;
	float RMSE[2];
	int number =0;


	for (int i = 0; i < nGCPCount; i++) {
		x[i] = pasGCPList[i].dfGCPPixel;
		y[i] = pasGCPList[i].dfGCPLine;
	}
	if (GDALGCPTransform(GCPTransform, FALSE, nGCPCount, x, y, z, su)) {
		for (int j = 0; j < nGCPCount; j++) {
			if (su) {
				number++;
				x_error += (x[j] - pasGCPList[j].dfGCPX) * (x[j] - pasGCPList[j].dfGCPX);
				y_error += (y[j] - pasGCPList[j].dfGCPY) * (y[j] - pasGCPList[j].dfGCPY);
			}
		}
	}
	RMSE[0] = x_error / number;
	RMSE[1] = y_error / number;
	cout << endl << "X方向误差：" << RMSE[0] << "			" << "Y方向误差" << RMSE[1] << endl;
	return RMSE;

}



