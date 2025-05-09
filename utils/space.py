# import struct

# def extract_rows(input_file, output_file, num_rows):
#     with open(input_file, 'rb') as f_in:

#         n, d = struct.unpack('ii', f_in.read(8))


#         if num_rows > n:
#             raise ValueError(f"num rows cannot be greater than the total number of rows in the file.")


#         data = []
#         for _ in range(num_rows):
#             row = f_in.read(d)  
#             data.append(row)


#     with open(output_file, 'wb') as f_out:

#         f_out.write(struct.pack('ii', num_rows, d))

#         for row in data:
#             f_out.write(row)

#     print(f"{num_rows} line saved to: {output_file}")


# extract_rows('/mnt/optane/wpq/dataset/space/query.i8bin', '/mnt/optane/wpq/dataset/space/query10K.i8bin', 10000)


# def verify_binary_file(binary_file, num_rows_to_print=2):
#     with open(binary_file, 'rb') as f:

#         n, d = struct.unpack('ii', f.read(8))


#         for i in range(num_rows_to_print):
#             row = f.read(d)
#             row_values = struct.unpack(f'{d}b', row)  
#             print(f"line {i}: {row_values}")


# verify_binary_file('/mnt/optane/wpq/dataset/space/query10K.i8bin')







import struct

def text_to_binary(text_file, binary_file):
    
    with open(text_file, 'r') as f:
        data = f.read().split()  

  
    data = list(map(int, data))

 
    n = data[0]
    topk = data[1]
    print(f"line number: {n}, topk: {topk}")

    ids = data[2:]


    with open(binary_file, 'wb') as f:
    
        f.write(struct.pack('ii', n, topk))
       
        for i in range(n):
            start = i * topk
            end = start + topk
            row = ids[start:end]
            f.write(struct.pack(f'{topk}i', *row))

    print(f"saved to: {binary_file}")
    

def read_binary_file(binary_file):
    with open(binary_file, 'rb') as f:
       
        n, topk = struct.unpack('ii', f.read(8))
        print(f"line number: {n},  topk: {topk}")

       
        for i in range(5):
            row = struct.unpack(f'{topk}i', f.read(4 * topk))
            print(f"line {i}: {row}")






text_to_binary('/mnt/optane/wpq/dataset/space/space1b_L2_gt_k100.txt', '/mnt/optane/wpq/dataset/space/space1B_L2_gt_k100.i8bin')



read_binary_file('/mnt/optane/wpq/dataset/space/space1B_L2_gt_k100.i8bin')