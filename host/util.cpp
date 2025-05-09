
#include "util.h"

// type==0: sift1b
// type==1: space
// type==2: sift1m
float *read_query(
    const char *filename,
    int type,
    int &nvecs,
    int &dim)
{
    
    printf("query path is %s\n", filename);
    if (type == 0)
    {
        FILE *f = fopen(filename, "rb");
        if (!f)
        {
            perror("Error opening file");
            return NULL;
        }

        assert(fread(&nvecs, sizeof(int), 1, f) == 1);
        assert(fread(&dim, sizeof(int), 1, f) == 1);

        int total_elements = nvecs * dim;
        uint8_t *int_data = new uint8_t[total_elements]();

        assert(fread(int_data, sizeof(uint8_t), total_elements, f) ==
               total_elements);

        fclose(f);

        float *float_data = new float[total_elements]();
        for (int i = 0; i < total_elements; i++)
        {
            float_data[i] = (float)int_data[i];
        }

        free(int_data);

        return float_data;
    }
    else if (type == 1)
    {
       
        FILE *f = fopen(filename, "rb");
        if (!f)
        {
            perror("Error opening file");
            return NULL;
        }

        assert(fread(&nvecs, sizeof(int), 1, f) == 1);
        assert(fread(&dim, sizeof(int), 1, f) == 1);

        int total_elements = nvecs * dim;
        int8_t *int_data = new int8_t[total_elements]();
        assert(fread(int_data, sizeof(int8_t), total_elements, f) ==
               total_elements);

        fclose(f);

        float *float_data = new float[total_elements]();
        for (int i = 0; i < total_elements; i++)
        {
            float_data[i] = (float)int_data[i];
        }

        free(int_data);

        return float_data;
    }
    else if (type == 2)
    {
        std::ifstream infile;
        infile.open(filename, std::ios::binary);
        std::vector<std::vector<float>> vectors;

        int nq = 0;
        int dim_ = 128;

        while (infile)
        {
            // Read dimension
            int dim;
            infile.read(reinterpret_cast<char *>(&dim), sizeof(int));
            if (!infile)
                break;
            assert(dim == dim_);

            // Read vector data
            std::vector<float> vec(dim);
            infile.read(reinterpret_cast<char *>(vec.data()), dim * sizeof(float));
            vectors.push_back(vec);
            nq++;
        }

        float *float_data = new float[nq * dim_]();
        for (int i = 0; i < nq; i++)
        {
            for (int j = 0; j < dim_; j++)
            {
                float_data[i * dim_ + j] = vectors[i][j];
            }
        }

        infile.close();

        return float_data;
    }
    else {
        fprintf(stderr, "Error: Invalid type %d\n", type);
        return nullptr;  // 明确返回空指针
    }
}


ID_TYPE *read_groundtruth(const char *filename, int &n, int &k)
{
    FILE *file = fopen(filename, "rb");
    if (!file)
    {
        perror("Failed to open file");
        return NULL;
    }

    if (fread(&n, sizeof(int), 1, file) != 1 ||
        fread(&k, sizeof(int), 1, file) != 1)
    {
        perror("Failed to read n or k");
        fclose(file);
        return NULL;
    }

  
    int *data = (int *)malloc(n * k * sizeof(int));
    if (!data)
    {
        perror("Failed to allocate memory");
        fclose(file);
        return NULL;
    }

   
    for (int i = 0; i < n; i++)
    {
       
        if (fread(&data[i * k], sizeof(int), k, file) != k)
        {
            perror("Failed to read data");
            free(data);
            fclose(file);
            return NULL;
        }
    }

   
    ID_TYPE *data_id = new ID_TYPE[n * k];
    for (int i = 0; i < n * k; i++)
    {
        data_id[i] = data[i];
    }

  
    fclose(file);

    return data_id;
}
