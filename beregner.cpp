#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <cstdlib>
#include "gdal_priv.h"
#include "cpl_conv.h" // for CPLMalloc()

// Struct to hold bounding box coordinates
struct BoundingBox {
    double minLon;
    double minLat;
    double maxLon;
    double maxLat;
};

// Function to parse BBOX string to BoundingBox struct
BoundingBox parseBBOX(const std::string& bboxStr) {
    BoundingBox bbox;
    sscanf(bboxStr.c_str(), "%lf,%lf,%lf,%lf", &bbox.minLon, &bbox.minLat, &bbox.maxLon, &bbox.maxLat);
    return bbox;
}

// Function to check if user BBOX is within allowed BBOX
bool isBBOXWithin(const BoundingBox& userBBOX, const BoundingBox& allowedBBOX) {
    return (userBBOX.minLon >= allowedBBOX.minLon &&
            userBBOX.maxLon <= allowedBBOX.maxLon &&
            userBBOX.minLat >= allowedBBOX.minLat &&
            userBBOX.maxLat <= allowedBBOX.maxLat);
}

// Function to calculate orthogonal projection
void calculateOrthogonalProjection(double* slopeData, int width, int height) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            double slope = slopeData[y * width + x];
            double slopeRad = slope * M_PI / 180.0; // Convert slope to radians

            // Assuming that dz/dx and dz/dy are equal and derived from the slope
            double dzdx = std::tan(slopeRad);
            double dzdy = std::tan(slopeRad);

            // Calculate orthogonal vector
            double normalX = -dzdx;
            double normalY = -dzdy;
            double normalZ = 1.0;
            double length = std::sqrt(normalX * normalX + normalY * normalY + normalZ * normalZ);
            normalX /= length;
            normalY /= length;
            normalZ /= length;

            // Calculate angles
            double slopeAngle = std::atan(std::sqrt(normalX * normalX + normalY * normalY)) * 180.0 / M_PI; // degrees from horizontal
            double orientation = std::atan2(normalY, normalX) * 180.0 / M_PI;
            if (orientation < 0) orientation += 360.0;

            // Print slope and orthogonal projection
            std::cout << "Pixel (" << x << ", " << y << "): Slope = " << slope << " degrees, "
                      << "Orthogonal Projection = " << slopeAngle << " degrees, "
                      << "Orientation = " << orientation << " degrees" << std::endl;
        }
    }
}

int main()
{
    GDALAllRegister();

    const char* wcsURL = "https://api.dataforsyningen.dk/dhm_wcs_DAF?service=WCS";
    const char* wcsLayer = "DHM_Overflade";
    const char* bboxStr = "10.0,54.0,15.0,57.0"; // Define your bounding box here (example coordinates)
    const char* token = "1d758ff07d7b45cc764053b5b585806c";
    const char* inputFilename = "input_dem.tif";
    const char* slopeFilename = "output_slope.tif";

    BoundingBox allowedBBOX = { 8.00830949937517, 54.4354651516217, 15.5979112056959, 57.7690657013977 };
    BoundingBox userBBOX = parseBBOX(bboxStr);

    // Check if user's BBOX is within the allowed BBOX
    if (!isBBOXWithin(userBBOX, allowedBBOX)) {
        std::cerr << "Error: The specified BBOX is out of the allowed range." << std::endl;
        return 1;
    }

    // Build WCS request URL
    std::string url = std::string(wcsURL) + "&REQUEST=GetCoverage&VERSION=2.0.1&COVERAGEID=" + wcsLayer + "&FORMAT=image/tiff&SUBSET=x(" + bboxStr + ")&SUBSET=y(" + bboxStr + ")&token=" + token;

    // Download DEM data using gdal_translate
    std::string translateCommand = "gdal_translate \"" + url + "\" " + inputFilename;
    std::cout << "Executing: " << translateCommand << std::endl;
    int translateResult = std::system(translateCommand.c_str());
    if (translateResult != 0) {
        std::cerr << "gdal_translate failed with error code: " << translateResult << std::endl;
        return translateResult;
    }

    // Calculate slope using gdaldem slope
    std::string slopeCommand = "gdaldem slope " + std::string(inputFilename) + " " + slopeFilename;
    std::cout << "Executing: " << slopeCommand << std::endl;
    int slopeResult = std::system(slopeCommand.c_str());
    if (slopeResult != 0) {
        std::cerr << "gdaldem slope failed with error code: " << slopeResult << std::endl;
        return slopeResult;
    }

    // Open the slope output file
    GDALDataset* poDataset = (GDALDataset*)GDALOpen(slopeFilename, GA_ReadOnly);
    if (poDataset == nullptr) {
        std::cerr << "GDALOpen failed - " << CPLGetLastErrorMsg() << std::endl;
        return 1;
    }

    GDALRasterBand* poBand = poDataset->GetRasterBand(1);
    int nXSize = poBand->GetXSize();
    int nYSize = poBand->GetYSize();

    double* pSlopeData = (double*)CPLMalloc(sizeof(double) * nXSize * nYSize);

    poBand->RasterIO(GF_Read, 0, 0, nXSize, nYSize, pSlopeData, nXSize, nYSize, GDT_Float64, 0, 0);

    calculateOrthogonalProjection(pSlopeData, nXSize, nYSize);

    GDALClose(poDataset);
    CPLFree(pSlopeData);

    std::cout << "Slope calculation completed and saved to " << slopeFilename << std::endl;

    return 0;
}
