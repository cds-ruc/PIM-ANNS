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

    print(f"saved to {binary_file}")
    

def read_binary_file(binary_file):
    with open(binary_file, 'rb') as f:
      
        n, topk = struct.unpack('ii', f.read(8))
        print(f"line number: {n}, topk: {topk}")

       
        for i in range(5):
            row = struct.unpack(f'{topk}i', f.read(4 * topk))
            print(f"line {i} : {row}")






text_to_binary('/mnt/optane/wpq/dataset/sift/sift1B_L2_gt_k100.txt', '/mnt/optane/wpq/dataset/sift/sift1B_L2_gt_k100.u8bin')



read_binary_file('/mnt/optane/wpq/dataset/sift/sift1B_L2_gt_k100.u8bin')