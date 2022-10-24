import os
from datetime import datetime
from nntool.api import NNGraph
from nntool_python_utils.coco_utils import test_coco_nntool
import argparse
import argcomplete

# create the top-level parser
parser = argparse.ArgumentParser(prog='test_nntool_coco')

parser.add_argument('--model_path', type=str, default="models/ssd_mobv1_075_quant.tflite",
                    help='path to tflite model')
parser.add_argument('--coco_path', type=str, default="/home/marco-gwt/Datasets/val2017",
                    help='path to coco validation dataset')
parser.add_argument('--coco_annotations', type=str, default="/home/marco-gwt/Datasets/annotations_trainval2017/annotations/instances_val2017.json",
                    help='path to coco annotations file')
argcomplete.autocomplete(parser)
args = parser.parse_args()
model_path = args.model_path
coco_path = args.coco_path
coco_ann_file = args.coco_annotations

now = datetime.now()
log_file = f"log_accuracy/{os.path.splitext(os.path.split(model_path)[-1])[0]}_nntool_{now.strftime('%H-%M_%m-%d-%Y')}.log"

G = NNGraph.load_graph(model_path, load_quantization=True)
G.adjust_order()
G.fusions("scaled_match_group")
G.quantize(
    None,
    graph_options={
        "use_ne16": True,
        "hwc": True
    }
)

test_coco_nntool(G, coco_path, coco_ann_file, log_file, mean=127.5, std=127.5, quantized_inference=True)
