import argparse
import argcomplete
from nntool.api import NNGraph
import sys
import os

def create_parser():
    # create the top-level parser
    parser = argparse.ArgumentParser(prog='nntool_generate_model')

    parser.add_argument('--trained_model',
                        help="path to tflite/onnx model to test")
    parser.add_argument('--at_model_path',
                        help="path to the generated model")
    parser.add_argument('--ram_device', default="AT_MEM_L3_HRAM",
                         choices=['AT_MEM_L3_HRAM', 'AT_MEM_L3_QSPIRAM', 'AT_MEM_L3_OSPIRAM', 'AT_MEM_L3_DEFAULTRAM'])
    parser.add_argument('--flash_device', default="AT_MEM_L3_HFLASH",
                         choices=['AT_MEM_L3_HFLASH', 'AT_MEM_L3_QSPIFLASH', 'AT_MEM_L3_OSPIFLASH', 'AT_MEM_L3_MRAMFLASH', 'AT_MEM_L3_DEFAULTFLASH'])
    parser.add_argument('--use_privileged_flash_device', action="store_true")
    return parser


def main():
    parser = create_parser()
    argcomplete.autocomplete(parser)
    args = parser.parse_args()

    at_model_path = args.at_model_path
    at_model_file = os.path.split(at_model_path)[-1]
    at_model_dir = os.path.split(at_model_path)[0]
    print(at_model_dir)

    G = NNGraph.load_graph(args.trained_model, load_quantization=True)
    G.adjust_order()
    G.fusions("scaled_match_group")
    G.quantize(
        None,
        graph_options={
            "use_ne16": True,
            "hwc": True
        }
    )
    print(G.qshow([G[0]]))
    G[0].allocate = True

    G.name = "ssd_mobilenet"
    G.generate(
        write_constants=True,
        settings={
            "tensor_directory": f"{at_model_dir}/tensors",
            "model_directory": f"{at_model_dir}/",
            "model_file": at_model_file,

            # Memory options
            "l1_size": 128000,
            "l2_size": 1300000,
            "l3_flash_device": args.flash_device,
            "l3_ram_device": args.ram_device,
            "l3_ram_ext_managed": True,
            "privileged_l3_flash_device": "AT_MEM_L3_MRAMFLASH" if args.use_privileged_flash_device else "",
            "privileged_l3_flash_size": 1900000,

            # Autotiler Graph Options
            "graph_size_opt": 2, # Os for layers and xtor,dxtor,runner
            "graph_const_exec_from_flash": True,
            "graph_monitor_cycles": True,
            "graph_produce_node_names": True,
            "graph_produce_operinfos": True,
        }
    )


if __name__ == '__main__':
    main()
