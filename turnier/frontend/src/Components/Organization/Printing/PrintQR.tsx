import React, { useEffect, useState } from 'react'
import PrintingA4 from './PrintingA4'
import { Stack, Typography } from '@mui/material'
import QRCode from "react-qr-code";
import { doGetRequest } from '../../Common/StaticFunctions';
import { useParams } from 'react-router-dom';

type Props = {}

const PrintQR = (props: Props) => {
    const params = useParams()

    const [qr, setqr] = useState("")

    console.log()
    useEffect(() => {
        doGetRequest(`${params.organization}/${params.date}/qr`).then((response) => {
            setqr(`${window.location.origin}/u/${params.organization}/${params.date}/${response.content}`)
        })
    }, [params.date, params.organization])

    return (
        <PrintingA4 generationType='QR-Code'>
            <Stack alignItems="center" gap={5}>
                <Typography variant="h3">Hier gehts zu den Live-Ergebnissen</Typography>

                <QRCode value={qr} />
            </Stack>
        </PrintingA4>
    )
}

export default PrintQR